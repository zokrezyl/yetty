#ifndef YETTY_YMSDF_GENERATOR_H
#define YETTY_YMSDF_GENERATOR_H

/*
 * yetty_ymsdf_generator - polymorphic MSDF CDB generator.
 *
 * Two implementations:
 *   - cpu: wraps yetty_ymsdf_gen (msdfgen library, multi-threaded).
 *   - gpu: wraps yetty_ymsdf_wgsl (WebGPU compute shader).
 *
 * Selected at yetty startup from the config key `msdf/generator`
 * (values: "cpu" or "gpu", default "gpu"). The chosen instance lives on
 * the gpu_context and is shared by every consumer (currently
 * ypaint-canvas font materialisation) so we can A/B the two backends by
 * flipping a single config knob.
 *
 * The generator is created in yetty_create after the WebGPU device is
 * ready (the gpu impl needs WGPUDevice + WGPUInstance up front). For
 * non-yetty harnesses (e.g. unit tests) the caller can construct an
 * impl directly via the create_cpu / create_gpu factories.
 */

#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ymsdf_generator;

/* Per-call config. ttf_path → cdb_path; the impl writes a single CDB at
 * cdb_path. font_size + pixel_range are the MSDF generation parameters
 * (default 32 / 4 if zero). */
struct yetty_ymsdf_generator_config {
    const char *ttf_path;
    const char *cdb_path;
    float font_size;
    float pixel_range;
};

struct yetty_ymsdf_generator_ops {
    /* "cpu" or "gpu" — used in logs / config diagnostics. */
    const char *(*name)(const struct yetty_ymsdf_generator *self);

    /* Generate a CDB. The impl is allowed to be slow; consumers gate
     * by file existence (cache) before calling. */
    struct yetty_ycore_void_result (*generate)(struct yetty_ymsdf_generator *self,
                                               const struct yetty_ymsdf_generator_config *cfg);

    /* Releases impl-owned state. Does NOT release the WGPUDevice/
     * WGPUInstance the gpu impl borrows. Handles NULL. */
    void (*destroy)(struct yetty_ymsdf_generator *self);
};

struct yetty_ymsdf_generator {
    const struct yetty_ymsdf_generator_ops *ops;
};

YETTY_YRESULT_DECLARE(yetty_ymsdf_generator_ptr, struct yetty_ymsdf_generator *);

/* Create a CPU generator. Stateless; generate() can run on any thread.
 * Always succeeds on platforms that link msdfgen. */
struct yetty_ymsdf_generator_ptr_result yetty_ymsdf_generator_create_cpu(void);

/* Create a GPU generator. device/instance are borrowed (caller owns them
 * for the generator's lifetime). shader_path is the absolute path to the
 * msdf_gen.wgsl compute shader; if NULL the impl falls back to its
 * exe-dir + ./shaders search chain. device + instance are typed as void *
 * to keep webgpu.h out of this header. */
struct yetty_ymsdf_generator_ptr_result yetty_ymsdf_generator_create_gpu(
    void *device, void *instance, const char *shader_path);

/* Resolve the generator selector from a config (`msdf/generator`,
 * default "gpu") and build the matching impl. shaders_dir is the
 * platform-resolved shader root used to locate msdf_gen.wgsl for the
 * gpu path. config is taken as void * to avoid pulling yconfig.h here
 * — internally cast back to struct yetty_yconfig_config *. */
struct yetty_ymsdf_generator_ptr_result yetty_ymsdf_generator_create_from_config(
    void *config, void *device, void *instance, const char *shaders_dir);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YMSDF_GENERATOR_H */
