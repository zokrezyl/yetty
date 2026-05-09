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
};

static const char *gpu_name(const struct yetty_ymsdf_generator *self)
{
    (void)self;
    return "gpu";
}

static struct yetty_ycore_void_result gpu_generate(struct yetty_ymsdf_generator *self,
                                                   const struct yetty_ymsdf_generator_config *cfg)
{
    struct gpu_gen *g = (struct gpu_gen *)self;
    if (!cfg || !cfg->ttf_path || !cfg->cdb_path) {
        return YETTY_ERR(yetty_ycore_void, "ymsdf gpu_generate: invalid config");
    }
    struct yetty_ymsdf_wgsl_config wc = {0};
    wc.ttf_path = cfg->ttf_path;
    wc.cdb_path = cfg->cdb_path;
    wc.font_size = cfg->font_size > 0 ? cfg->font_size : 32.0f;
    wc.pixel_range = cfg->pixel_range > 0 ? cfg->pixel_range : 4.0f;
    wc.device = g->device;
    wc.instance = g->instance;
    wc.shader_path = g->shader_path;
    return yetty_ymsdf_wgsl_config_generate(&wc);
}

static void gpu_destroy(struct yetty_ymsdf_generator *self)
{
    if (!self) {
        return;
    }
    struct gpu_gen *g = (struct gpu_gen *)self;
    free(g->shader_path);
    free(g);
}

static const struct yetty_ymsdf_generator_ops GPU_OPS = {
    .name = gpu_name,
    .generate = gpu_generate,
    .destroy = gpu_destroy,
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
