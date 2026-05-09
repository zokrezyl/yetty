/*
 * factory.c - select MSDF generator based on config (`msdf/generator`).
 *
 * Default: "gpu" (compute shader, faster). Falling back to "cpu" gives the
 * proven msdfgen path while we shake out the WGSL shader's curve artefacts.
 * Config invariant: any non-"cpu" / non-"gpu" value is rejected loudly so
 * a typo doesn't silently pick the wrong backend.
 */

#include <yetty/ymsdf/generator.h>
#include <yetty/yconfig/config.h>
#include <yetty/ytrace/ytrace.h>

#include <stdio.h>
#include <string.h>

struct yetty_ymsdf_generator_ptr_result yetty_ymsdf_generator_create_from_config(
    void *config_ptr, void *device, void *instance, const char *shaders_dir)
{
    struct yetty_yconfig_config *config = (struct yetty_yconfig_config *)config_ptr;
    const char *kind =
        config && config->ops ? config->ops->get_string(config, "msdf/generator", "gpu") : "gpu";
    if (!kind) {
        kind = "gpu";
    }

    if (strcmp(kind, "cpu") == 0) {
        ydebug("ymsdf: selecting CPU generator (msdf/generator=cpu)");
        return yetty_ymsdf_generator_create_cpu();
    }
    if (strcmp(kind, "gpu") == 0) {
        char shader_path[1024] = {0};
        const char *sp = NULL;
        if (shaders_dir && *shaders_dir) {
            snprintf(shader_path, sizeof(shader_path), "%s/msdf_gen.wgsl", shaders_dir);
            sp = shader_path;
        }
        ydebug("ymsdf: selecting GPU generator (msdf/generator=gpu, shader='%s')",
               sp ? sp : "<auto>");
        return yetty_ymsdf_generator_create_gpu(device, instance, sp);
    }
    yerror("ymsdf: unknown msdf/generator value '%s' (expected 'cpu' or 'gpu')", kind);
    return YETTY_ERR(yetty_ymsdf_generator_ptr, "ymsdf: msdf/generator must be 'cpu' or 'gpu'");
}
