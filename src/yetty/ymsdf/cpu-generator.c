/*
 * cpu-generator.c - polymorphic wrapper around yetty_ymsdf_gen (CPU msdfgen).
 *
 * The underlying CPU API takes (ttf_path, output_dir) and produces
 * <dir>/<ttf_basename>.cdb. The polymorphic API takes a full cdb_path,
 * so we split it, invoke the CPU gen, and rename the output if the
 * derived basename doesn't match. In practice ydraw-canvas already
 * names them consistently (pdf_<hash>.ttf → pdf_<hash>.cdb) so the
 * rename is a no-op there.
 */

#include <yetty/ymsdf/generator.h>
#include <yetty/ymsdf-gen/ymsdf-gen.h>
#include <yetty/ytrace/ytrace.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct cpu_gen {
    struct yetty_ymsdf_generator base;
};

static const char *cpu_name(const struct yetty_ymsdf_generator *self)
{
    (void)self;
    return "cpu";
}

/* Split "/some/dir/foo.cdb" into ("/some/dir", "foo"). dir/stem must
 * each be at least 1024 chars. Returns 0 on success, -1 if the path is
 * malformed (no '/' or no '.cdb' suffix). */
static int split_cdb_path(const char *cdb_path, char *dir, size_t dir_cap, char *stem,
                          size_t stem_cap)
{
    const char *slash = strrchr(cdb_path, '/');
    if (!slash) {
        return -1;
    }
    size_t dlen = (size_t)(slash - cdb_path);
    if (dlen + 1 > dir_cap) {
        return -1;
    }
    memcpy(dir, cdb_path, dlen);
    dir[dlen] = '\0';

    const char *base = slash + 1;
    const char *dot = strrchr(base, '.');
    if (!dot || strcmp(dot, ".cdb") != 0) {
        return -1;
    }
    size_t slen = (size_t)(dot - base);
    if (slen + 1 > stem_cap) {
        return -1;
    }
    memcpy(stem, base, slen);
    stem[slen] = '\0';
    return 0;
}

static const char *path_basename_stem(const char *path, char *out, size_t out_cap)
{
    const char *slash = strrchr(path, '/');
    const char *base = slash ? slash + 1 : path;
    const char *dot = strrchr(base, '.');
    size_t n = dot ? (size_t)(dot - base) : strlen(base);
    if (n + 1 > out_cap) {
        return NULL;
    }
    memcpy(out, base, n);
    out[n] = '\0';
    return out;
}

static struct yetty_ycore_void_result cpu_generate(struct yetty_ymsdf_generator *self,
                                                   const struct yetty_ymsdf_generator_config *cfg)
{
    (void)self;
    if (!cfg || !cfg->ttf_path || !cfg->cdb_path) {
        return YETTY_ERR(yetty_ycore_void, "ymsdf cpu_generate: invalid config");
    }

    char dir[1024], stem[256];
    if (split_cdb_path(cfg->cdb_path, dir, sizeof(dir), stem, sizeof(stem)) < 0) {
        return YETTY_ERR(yetty_ycore_void, "ymsdf cpu_generate: cdb_path must end in .cdb");
    }

    struct yetty_ymsdf_gen_config gen = {0};
    gen.ttf_path = cfg->ttf_path;
    gen.output_dir = dir;
    gen.font_size = cfg->font_size > 0 ? cfg->font_size : 32.0f;
    gen.pixel_range = cfg->pixel_range > 0 ? cfg->pixel_range : 4.0f;
    gen.thread_count = 0; /* auto */
    gen.all_glyphs = 1;
    struct yetty_ycore_void_result r = yetty_ymsdf_gen_config_cpu_generate(&gen);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "ymsdf cpu_generate: cpu impl failed");

    /* CPU gen names the output after the TTF basename. Rename if the
     * caller asked for a different stem. */
    char ttf_stem[256];
    if (!path_basename_stem(cfg->ttf_path, ttf_stem, sizeof(ttf_stem))) {
        return YETTY_ERR(yetty_ycore_void, "ymsdf cpu_generate: ttf basename too long");
    }
    if (strcmp(ttf_stem, stem) != 0) {
        char produced[1280];
        snprintf(produced, sizeof(produced), "%s/%s.cdb", dir, ttf_stem);
        if (rename(produced, cfg->cdb_path) != 0) {
            return YETTY_ERR(yetty_ycore_void, "ymsdf cpu_generate: rename to cdb_path failed");
        }
    }
    return YETTY_OK_VOID();
}

static void cpu_destroy(struct yetty_ymsdf_generator *self)
{
    free(self);
}

static const struct yetty_ymsdf_generator_ops CPU_OPS = {
    .name = cpu_name,
    .generate = cpu_generate,
    .destroy = cpu_destroy,
};

struct yetty_ymsdf_generator_ptr_result yetty_ymsdf_generator_create_cpu(void)
{
    struct cpu_gen *g = calloc(1, sizeof(*g));
    if (!g) {
        return YETTY_ERR(yetty_ymsdf_generator_ptr, "alloc cpu generator failed");
    }
    g->base.ops = &CPU_OPS;
    return YETTY_OK(yetty_ymsdf_generator_ptr, &g->base);
}
