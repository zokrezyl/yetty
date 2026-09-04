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
 * ydraw-canvas font materialisation) so we can A/B the two backends by
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

/* A staged generation in flight — backend-specific, opaque here. */
struct yetty_ymsdf_job;
YETTY_YRESULT_DECLARE(yetty_ymsdf_job_ptr, struct yetty_ymsdf_job *);

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

    /* Staged generation — optional, all NULL on a backend with no separable
     * CPU stage (the CPU backend). Lets a batch overlap the CPU work of one
     * font with the GPU pass of another (yetty_ymsdf_generator_ensure_cdb_batch):
     *   prepare   any thread, must not touch the device: outlines, layout.
     *   submit    the device's thread: queue the font's GPU work, return.
     *   readback  the device's thread: wait for that font, read it back.
     *   finish    any thread: writes cfg->cdb_path.
     * job_destroy after finish, or after any failure. */
    struct yetty_ymsdf_job_ptr_result (*prepare)(struct yetty_ymsdf_generator *self,
                                                 const struct yetty_ymsdf_generator_config *cfg);
    struct yetty_ycore_void_result (*submit)(struct yetty_ymsdf_generator *self,
                                             struct yetty_ymsdf_job *job);
    struct yetty_ycore_void_result (*readback)(struct yetty_ymsdf_generator *self,
                                               struct yetty_ymsdf_job *job);
    struct yetty_ycore_void_result (*finish)(struct yetty_ymsdf_generator *self,
                                             struct yetty_ymsdf_job *job);
    void (*job_destroy)(struct yetty_ymsdf_generator *self, struct yetty_ymsdf_job *job);
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
struct yetty_ymsdf_generator_ptr_result yetty_ymsdf_generator_create_gpu(void *device,
                                                                         void *instance,
                                                                         const char *shader_path);

/* Resolve the generator selector from a config (`msdf/generator`,
 * default "gpu") and build the matching impl. shaders_dir is the
 * platform-resolved shader root used to locate msdf_gen.wgsl for the
 * gpu path. config is taken as void * to avoid pulling yconfig.h here
 * — internally cast back to struct yetty_yconfig_config *. */
struct yetty_ymsdf_generator_ptr_result yetty_ymsdf_generator_create_from_config(
    void *config, void *device, void *instance, const char *shaders_dir);

/* Generate cdb_path from ttf_path unless cdb_path already exists (a CDB is
 * a cache keyed by its path; consumers gate on existence and never re-check
 * contents). The atlas is built inside a private scratch directory next to
 * the destination and renamed into place once complete, so cdb_path never
 * holds a truncated file — a crash mid-generation leaves the cache empty,
 * not poisoned. Two processes racing on the same first run each build their
 * own copy; whichever renames last wins, with a complete file either way.
 * generator may be NULL only for a cache hit. *out_generated (optional) is
 * set to 1 when an atlas was built, 0 on a cache hit. */
struct yetty_ycore_void_result yetty_ymsdf_generator_ensure_cdb(
    struct yetty_ymsdf_generator *generator, const char *ttf_path, const char *cdb_path,
    int *out_generated);

/* One atlas of a batch: the two paths in, the outcome out. */
struct yetty_ymsdf_ensure_item {
    const char *ttf_path;
    const char *cdb_path;
    int generated;                         /* out: 1 when an atlas was built */
    struct yetty_ycore_void_result result; /* out: this atlas's outcome; the caller
                                              destroys an error */
};

/* ensure_cdb for several atlases at once, with the same cache and scratch
 * contract. On a backend with staged generation the CPU stages of every
 * miss run concurrently on their own threads; the device stages run on the
 * calling thread — every font submitted first, then read back in turn, so
 * the GPU works on the next font while the CPU copies the previous one —
 * and the device is never used from more than one thread. A backend
 * without stages builds the misses one after the other. Per-atlas outcomes
 * land in items[]; the return value only reports a failure of the batch
 * machinery itself. */
struct yetty_ycore_void_result yetty_ymsdf_generator_ensure_cdb_batch(
    struct yetty_ymsdf_generator *generator, struct yetty_ymsdf_ensure_item *items, size_t count);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YMSDF_GENERATOR_H */
