/*
 * gpu-generator.c - polymorphic wrapper around yetty_ymsdf_wgsl (compute
 * shader). Captures the WGPUDevice + WGPUInstance at create time so the
 * canvas can call generate(cfg) without threading them through.
 */

#include <yetty/ymsdf/generator.h>
#include <yetty/ymsdf-wgsl/ymsdf-wgsl.h>
#include <yetty/ytrace/ytrace.h>

#include <stdlib.h>
#include <string.h>

struct gpu_gen {
    struct yetty_ymsdf_generator base;
    void *device;
    void *instance;
    char *shader_path; /* owned, may be NULL */
    /* The compiled compute pipeline, built on first use on the device's
     * thread and kept for the generator's lifetime. */
    struct yetty_ymsdf_wgsl_pipeline *pipeline;
};

/* The pipeline, compiling it on first use. Device thread only. */
static struct yetty_ymsdf_wgsl_pipeline_ptr_result gpu_pipeline(struct gpu_gen *g)
{
    if (!g->pipeline) {
        struct yetty_ymsdf_wgsl_pipeline_ptr_result created =
            yetty_ymsdf_wgsl_pipeline_create(g->device, g->instance, g->shader_path);
        YETTY_RETURN_IF_ERR(yetty_ymsdf_wgsl_pipeline_ptr, created,
                            "ymsdf gpu: compute pipeline failed");
        g->pipeline = created.value;
    }
    return YETTY_OK(yetty_ymsdf_wgsl_pipeline_ptr, g->pipeline);
}

static const char *gpu_name(const struct yetty_ymsdf_generator *self)
{
    (void)self;
    return "gpu";
}

/* The wgsl config for one call: the caller's paths + sizes, our device. */
static int gpu_wgsl_config(const struct gpu_gen *g, const struct yetty_ymsdf_generator_config *cfg,
                           struct yetty_ymsdf_wgsl_config *wc)
{
    if (!cfg || !cfg->ttf_path || !cfg->cdb_path) {
        return -1;
    }
    memset(wc, 0, sizeof(*wc));
    wc->ttf_path = cfg->ttf_path;
    wc->cdb_path = cfg->cdb_path;
    wc->font_size = cfg->font_size > 0 ? cfg->font_size : 32.0f;
    wc->pixel_range = cfg->pixel_range > 0 ? cfg->pixel_range : 4.0f;
    wc->device = g->device;
    wc->instance = g->instance;
    wc->shader_path = g->shader_path;
    return 0;
}

static struct yetty_ycore_void_result gpu_generate(struct yetty_ymsdf_generator *self,
                                                   const struct yetty_ymsdf_generator_config *cfg)
{
    struct gpu_gen *g = (struct gpu_gen *)self;
    struct yetty_ymsdf_wgsl_config wc;
    if (gpu_wgsl_config(g, cfg, &wc) < 0) {
        return YETTY_ERR(yetty_ycore_void, "ymsdf gpu_generate: invalid config");
    }
    struct yetty_ymsdf_wgsl_pipeline_ptr_result pipeline = gpu_pipeline(g);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, pipeline, "ymsdf gpu_generate: pipeline");

    struct yetty_ymsdf_wgsl_job_ptr_result prepared = yetty_ymsdf_wgsl_job_prepare(&wc);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, prepared, "ymsdf gpu_generate: prepare failed");
    struct yetty_ymsdf_wgsl_job *job = prepared.value;
    struct yetty_ycore_void_result rendered = yetty_ymsdf_wgsl_job_render(job, pipeline.value);
    if (YETTY_IS_ERR(rendered)) {
        yetty_ymsdf_wgsl_job_destroy(job);
        return YETTY_ERR(yetty_ycore_void, "ymsdf gpu_generate: render failed", rendered);
    }
    struct yetty_ycore_void_result written = yetty_ymsdf_wgsl_job_write(job);
    yetty_ymsdf_wgsl_job_destroy(job);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, written, "ymsdf gpu_generate: write failed");
    return YETTY_OK_VOID();
}

/* Staged ops: a job is the wgsl job, cast through the opaque handle. */
static struct yetty_ymsdf_job_ptr_result gpu_prepare(struct yetty_ymsdf_generator *self,
                                                     const struct yetty_ymsdf_generator_config *cfg)
{
    struct gpu_gen *g = (struct gpu_gen *)self;
    struct yetty_ymsdf_wgsl_config wc;
    if (gpu_wgsl_config(g, cfg, &wc) < 0) {
        return YETTY_ERR(yetty_ymsdf_job_ptr, "ymsdf gpu_prepare: invalid config");
    }
    struct yetty_ymsdf_wgsl_job_ptr_result prepared = yetty_ymsdf_wgsl_job_prepare(&wc);
    YETTY_RETURN_IF_ERR(yetty_ymsdf_job_ptr, prepared, "ymsdf gpu_prepare: wgsl prepare failed");
    return YETTY_OK(yetty_ymsdf_job_ptr, (struct yetty_ymsdf_job *)prepared.value);
}

static struct yetty_ycore_void_result gpu_submit(struct yetty_ymsdf_generator *self,
                                                 struct yetty_ymsdf_job *job)
{
    struct gpu_gen *g = (struct gpu_gen *)self;
    struct yetty_ymsdf_wgsl_pipeline_ptr_result pipeline = gpu_pipeline(g);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, pipeline, "ymsdf gpu_submit: pipeline");
    return yetty_ymsdf_wgsl_job_submit((struct yetty_ymsdf_wgsl_job *)job, pipeline.value);
}

static struct yetty_ycore_void_result gpu_readback(struct yetty_ymsdf_generator *self,
                                                   struct yetty_ymsdf_job *job)
{
    (void)self;
    return yetty_ymsdf_wgsl_job_readback((struct yetty_ymsdf_wgsl_job *)job);
}

static struct yetty_ycore_void_result gpu_finish(struct yetty_ymsdf_generator *self,
                                                 struct yetty_ymsdf_job *job)
{
    (void)self;
    return yetty_ymsdf_wgsl_job_write((struct yetty_ymsdf_wgsl_job *)job);
}

static void gpu_job_destroy(struct yetty_ymsdf_generator *self, struct yetty_ymsdf_job *job)
{
    (void)self;
    yetty_ymsdf_wgsl_job_destroy((struct yetty_ymsdf_wgsl_job *)job);
}

static void gpu_destroy(struct yetty_ymsdf_generator *self)
{
    if (!self) {
        return;
    }
    struct gpu_gen *g = (struct gpu_gen *)self;
    yetty_ymsdf_wgsl_pipeline_destroy(g->pipeline);
    free(g->shader_path);
    free(g);
}

static const struct yetty_ymsdf_generator_ops GPU_OPS = {
    .name = gpu_name,
    .generate = gpu_generate,
    .destroy = gpu_destroy,
    .prepare = gpu_prepare,
    .submit = gpu_submit,
    .readback = gpu_readback,
    .finish = gpu_finish,
    .job_destroy = gpu_job_destroy,
};

struct yetty_ymsdf_generator_ptr_result yetty_ymsdf_generator_create_gpu(void *device,
                                                                         void *instance,
                                                                         const char *shader_path)
{
    if (!device || !instance) {
        return YETTY_ERR(yetty_ymsdf_generator_ptr,
                         "ymsdf gpu generator: device and instance required");
    }
    struct gpu_gen *g = calloc(1, sizeof(*g));
    if (!g) {
        return YETTY_ERR(yetty_ymsdf_generator_ptr, "alloc gpu generator failed");
    }
    g->base.ops = &GPU_OPS;
    g->device = device;
    g->instance = instance;
    if (shader_path) {
        g->shader_path = strdup(shader_path);
        if (!g->shader_path) {
            free(g);
            return YETTY_ERR(yetty_ymsdf_generator_ptr, "alloc gpu generator shader_path failed");
        }
    }
    return YETTY_OK(yetty_ymsdf_generator_ptr, &g->base);
}
