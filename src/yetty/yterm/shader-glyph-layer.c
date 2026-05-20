#include <yetty/yplatform/compat.h> /* dirent on POSIX, Win32 shim on MSVC */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <yetty/yconfig/config.h>
#include <yetty/yevent/event-loop.h>
#include <yetty/yevent/event.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/util.h>
#include <yetty/yetty/yetty.h>
#include <yetty/yrender/gpu-resource-set.h>
#include <yetty/yrender/render-target.h>
#include <yetty/ytrace/ytrace.h>
#include <yetty/yterm/shader-glyph-layer.h>
#include <yetty/ywire/wire-statemachine.h>
#include <yetty/yterm/text-layer.h>

/* Uniform slots */
#define U_GRID_SIZE 0
#define U_CELL_SIZE 1
#define U_TIME 2
#define U_VZ_SCALE 3
#define U_VZ_OFF 4
#define U_COUNT 5

static inline void set_grid_size(struct yetty_ydraw_gpu_resource_set *rs, float cols,
                                 float rows)
{
    rs->uniforms[U_GRID_SIZE].vec2[0] = cols;
    rs->uniforms[U_GRID_SIZE].vec2[1] = rows;
}

static inline void set_cell_size(struct yetty_ydraw_gpu_resource_set *rs, float w, float h)
{
    rs->uniforms[U_CELL_SIZE].vec2[0] = w;
    rs->uniforms[U_CELL_SIZE].vec2[1] = h;
}

static inline void set_time(struct yetty_ydraw_gpu_resource_set *rs, float t)
{
    rs->uniforms[U_TIME].f32 = t;
}

static inline void set_visual_zoom(struct yetty_ydraw_gpu_resource_set *rs, float scale,
                                   float off_x, float off_y)
{
    rs->uniforms[U_VZ_SCALE].f32 = scale;
    rs->uniforms[U_VZ_OFF].vec2[0] = off_x;
    rs->uniforms[U_VZ_OFF].vec2[1] = off_y;
}

static void init_uniforms(struct yetty_ydraw_gpu_resource_set *rs)
{
    rs->uniform_count = U_COUNT;

    rs->uniforms[U_GRID_SIZE] =
        (struct yetty_yrender_uniform){"grid_size", YETTY_YRENDER_UNIFORM_VEC2};
    rs->uniforms[U_CELL_SIZE] =
        (struct yetty_yrender_uniform){"cell_size", YETTY_YRENDER_UNIFORM_VEC2};
    rs->uniforms[U_TIME] = (struct yetty_yrender_uniform){"time", YETTY_YRENDER_UNIFORM_F32};
    rs->uniforms[U_VZ_SCALE] =
        (struct yetty_yrender_uniform){"visual_zoom_scale", YETTY_YRENDER_UNIFORM_F32};
    rs->uniforms[U_VZ_OFF] =
        (struct yetty_yrender_uniform){"visual_zoom_off", YETTY_YRENDER_UNIFORM_VEC2};

    set_visual_zoom(rs, 1.0f, 0.0f, 0.0f);
    set_time(rs, 0.0f);
}

/* Per-instance entry uploaded to buffers[1]. One entry per shader-glyph
 * cell visible this frame; the vertex shader fetches it by instance_index,
 * positions a quad over the cell, and passes local_id + colors through to
 * the fragment shader. Keeping it tiny (8 bytes) keeps the upload cheap. */
struct shader_glyph_instance {
    uint32_t cell_index; /* row * cols + col within the cell buffer */
    uint32_t local_id;   /* index into the generated render_shader_glyph dispatcher */
};

/* Layer struct - embeds base as first member */
struct yetty_yterm_shader_glyph_layer {
    struct yetty_yrender_terminal_layer base;
    /* Borrowed: text-layer owns the cell buffer; we just point at it. */
    struct yetty_yrender_terminal_layer *text_layer;
    /* Final assembled shader source (template with glyph code spliced in). */
    char *shader_source;
    size_t shader_source_size;
    struct yetty_ydraw_gpu_resource_set rs;
    /* CPU-side animation clock origin. Time uniform is (now - t0). */
    struct timespec t0;

    /* Per-frame cell instance list. Built in get_gpu_resource_set by
     * scanning the text-layer's cell buffer for cells whose glyph_index
     * has bit-31 set. rs.buffers[1] points here and the draw is instanced
     * with rs.instance_count = instance_count. */
    struct shader_glyph_instance *instances;
    size_t instance_cap;
    uint32_t instance_count;

    /* Animation timer. Drives request_render at target_fps while there's
     * any shader-glyph cell on screen; stays stopped (zero-cost) on idle
     * terminals so the input→render loop can quiesce. Borrowed event loop;
     * timer_id valid only when timer_created. */
    struct yetty_yevent_event_loop *event_loop;
    yetty_yevent_timer_id timer_id;
    int timer_created;
    int timer_running;
    struct yetty_yevent_event_listener listener;
};

/* -- glyph-shader assembly --------------------------------------------------
 *
 * At create time we scan `<shaders_dir>/glyph-shaders/0xNNNN-*.wgsl`, read
 * each file, sort by local_id, concatenate all bodies, then emit a generated
 * `render_shader_glyph(local_id, ...)` switch dispatcher. The result is
 * spliced into shader-glyph-layer.wgsl at the `// SHADER_GLYPHS_PLACEHOLDER`
 * marker. No build-time codegen — adding a glyph is just dropping a .wgsl
 * file into the directory and relaunching.
 */

struct yetty_yterm_glyph_entry {
    uint32_t local_id;
    char *body;
    size_t body_size;
};

static int glyph_entry_cmp(const void *a, const void *b)
{
    uint32_t la = ((const struct yetty_yterm_glyph_entry *)a)->local_id;
    uint32_t lb = ((const struct yetty_yterm_glyph_entry *)b)->local_id;
    return (la > lb) - (la < lb);
}

/* Match a glyph file's basename (e.g. "0x0000-spinner.wgsl") against an
 * entry in shaders/preload/glyphs (e.g. "0x0000-spinner"). Returns 1 on
 * match. We compare case-insensitively because the YAML config tends to
 * have "0xeff5-..." in lowercase while filenames vary, and the hex
 * prefix is the only part that risks case skew. */
static int glyph_name_matches(const char *filename, const char *want)
{
    size_t flen = strlen(filename);
    if (flen <= 5 || strcmp(filename + flen - 5, ".wgsl") != 0) {
        return 0;
    }
    size_t stem_len = flen - 5;
    size_t wlen = strlen(want);
    if (wlen != stem_len) {
        return 0;
    }
    for (size_t i = 0; i < stem_len; i++) {
        char a = filename[i];
        char b = want[i];
        if (a >= 'A' && a <= 'Z') {
            a = (char)(a - 'A' + 'a');
        }
        if (b >= 'A' && b <= 'Z') {
            b = (char)(b - 'A' + 'a');
        }
        if (a != b) {
            return 0;
        }
    }
    return 1;
}

/* True if `filename` appears in shaders/preload/glyphs in the user
 * config. Strict allow-list: missing key, empty list, or NULL config →
 * load NOTHING. The user must explicitly enumerate which glyphs to
 * compile in. This avoids dragging in heavy shaders (butterfly-flock,
 * hg-sdf, etc.) that hang the GPU on Intel HD 530 / Mesa-ANV when
 * they're merely included in the merged WGSL even without runtime
 * invocation. */
static int glyph_allowed(struct yetty_yconfig_config *config, const char *filename)
{
    if (!config) {
        return 0;
    }
    int n = config->ops->get_array_count(config, "shaders/preload/glyphs");
    for (int i = 0; i < n; i++) {
        const char *want =
            config->ops->get_array_item(config, "shaders/preload/glyphs", i, NULL);
        if (want && glyph_name_matches(filename, want)) {
            return 1;
        }
    }
    return 0;
}

/* Returns a yetty_ycore_buffer with the assembled WGSL (data is malloc'd,
 * caller frees via free(buf.value.data)).
 *
 * Files starting with '_' are prelude libraries (e.g. _util.wgsl providing
 * util_hash, util_valueNoise, util_colorNoise - copied from yetty-poc). They
 * are concatenated FIRST, in name-sorted order, so glyph functions can call
 * their helpers. Files starting with '0x' are glyphs, sorted by local-id —
 * filtered through shaders/preload/glyphs in the user's yetty config. */
static struct yetty_ycore_buffer_result assemble_glyph_shaders(
    const char *glyph_dir, struct yetty_yconfig_config *config)
{
    DIR *d = opendir(glyph_dir);
    if (!d) {
        ywarn("glyph-shaders: opendir(%s) failed", glyph_dir);
        return YETTY_ERR(yetty_ycore_buffer, "glyph-shaders: opendir failed");
    }
    int allow_list_size =
        config ? config->ops->get_array_count(config, "shaders/preload/glyphs") : 0;
    yinfo("glyph-shaders: explicit allow-list, %d glyphs requested", allow_list_size);

    struct yetty_yterm_glyph_entry *entries = NULL;
    size_t cap = 0, n = 0;

    /* Preludes (`_*.wgsl`) — concatenated first, name-sorted. */
    struct yetty_ycore_buffer prelude_bufs[8];
    char prelude_names[8][128];
    size_t prelude_count = 0;

    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        const char *name = de->d_name;
        size_t len = strlen(name);
        if (len < 6) {
            continue;
        }
        if (strcmp(name + len - 5, ".wgsl") != 0) {
            continue;
        }

        char path[768];
        snprintf(path, sizeof(path), "%s/%s", glyph_dir, name);

        if (name[0] == '_') {
            /* Prelude (shared helpers like _util.wgsl). Only worth
             * concatenating if at least one glyph shader is actually
             * being loaded — without glyphs the helpers are dead code. */
            if (allow_list_size <= 0) {
                ydebug("glyph-shaders: %s skipped (no glyphs requested)", name);
                continue;
            }
            if (prelude_count >= 8) {
                ywarn("glyph-shaders: too many prelude files (max 8)");
                continue;
            }
            struct yetty_ycore_buffer_result br = yetty_ycore_read_file(path);
            if (YETTY_IS_ERR(br)) {
                ywarn("glyph-shaders: prelude %s: %s", path, br.error.msg);
                yetty_ycore_error_destroy(br.error);
                continue;
            }
            prelude_bufs[prelude_count] = br.value;
            strncpy(prelude_names[prelude_count], name, sizeof(prelude_names[0]) - 1);
            prelude_names[prelude_count][sizeof(prelude_names[0]) - 1] = 0;
            prelude_count++;
            continue;
        }

        if (strncmp(name, "0x", 2) != 0) {
            continue;
        }

        if (!glyph_allowed(config, name)) {
            ydebug("glyph-shaders: %s skipped (not in shaders/preload/glyphs)", name);
            continue;
        }

        uint32_t local_id = (uint32_t)strtoul(name + 2, NULL, 16);

        struct yetty_ycore_buffer_result br = yetty_ycore_read_file(path);
        if (YETTY_IS_ERR(br)) {
            ywarn("glyph-shaders: read %s: %s", path, br.error.msg);
            yetty_ycore_error_destroy(br.error);
            continue;
        }
        ydebug("glyph-shaders: %s loaded (%zu B)", name, br.value.size);

        if (n >= cap) {
            size_t new_cap = cap ? cap * 2 : 32;
            struct yetty_yterm_glyph_entry *grown = realloc(entries, new_cap * sizeof(*entries));
            if (!grown) {
                free(br.value.data);
                break;
            }
            entries = grown;
            cap = new_cap;
        }
        entries[n].local_id = local_id;
        entries[n].body = (char *)br.value.data;
        entries[n].body_size = br.value.size;
        n++;
    }
    closedir(d);

    qsort(entries, n, sizeof(struct yetty_yterm_glyph_entry), glyph_entry_cmp);

    /* Compute output size: prelude bodies + glyph bodies + dispatcher slack. */
    size_t total = 256 + n * 128;
    for (size_t i = 0; i < prelude_count; i++) {
        total += prelude_bufs[i].size + 1;
    }
    for (size_t i = 0; i < n; i++) {
        total += entries[i].body_size + 1;
    }
    char *out = malloc(total);
    if (!out) {
        for (size_t i = 0; i < prelude_count; i++) {
            free(prelude_bufs[i].data);
        }
        for (size_t i = 0; i < n; i++) {
            free(entries[i].body);
        }
        free(entries);
        return YETTY_ERR(yetty_ycore_buffer, "glyph-shaders: alloc failed");
    }

    size_t off = 0;
    /* Preludes first so glyph functions can call them. */
    for (size_t i = 0; i < prelude_count; i++) {
        memcpy(out + off, prelude_bufs[i].data, prelude_bufs[i].size);
        off += prelude_bufs[i].size;
        out[off++] = '\n';
        free(prelude_bufs[i].data);
    }
    for (size_t i = 0; i < n; i++) {
        memcpy(out + off, entries[i].body, entries[i].body_size);
        off += entries[i].body_size;
        out[off++] = '\n';
    }

    int w = snprintf(out + off, total - off,
                     "fn render_shader_glyph(local_id: u32, uv: vec2<f32>, time: f32,\n"
                     "                       fg: vec3<f32>, bg: vec3<f32>,\n"
                     "                       pixel_pos: vec2<f32>) -> vec3<f32> {\n"
                     "    switch (local_id) {\n");
    if (w > 0) {
        off += (size_t)w;
    }

    for (size_t i = 0; i < n; i++) {
        w = snprintf(out + off, total - off,
                     "        case %uu: { return shader_glyph_%u(uv, time, fg, bg, pixel_pos); }\n",
                     entries[i].local_id, entries[i].local_id);
        if (w > 0) {
            off += (size_t)w;
        }
    }

    w = snprintf(out + off, total - off,
                 "        default: { return mix(bg, fg, 0.5); }\n"
                 "    }\n"
                 "}\n");
    if (w > 0) {
        off += (size_t)w;
    }

    for (size_t i = 0; i < n; i++) {
        free(entries[i].body);
    }
    free(entries);

    ydebug("glyph-shaders: assembled %zu prelude + %zu glyphs, %zu bytes WGSL", prelude_count, n,
           off);
    struct yetty_ycore_buffer outbuf = {
        .data = (uint8_t *)out,
        .size = off,
        .capacity = total,
    };
    return YETTY_OK(yetty_ycore_buffer, outbuf);
}

/* Substitute the first occurrence of `marker` in `template` with `replacement`.
 * Returns malloc'd buffer; caller frees. *out_size set to result length. */
static char *splice_marker(const char *template, size_t template_size, const char *marker,
                           const char *replacement, size_t replacement_size, size_t *out_size)
{
    size_t marker_len = strlen(marker);
    const char *p = NULL;
    for (size_t i = 0; i + marker_len <= template_size; i++) {
        if (memcmp(template + i, marker, marker_len) == 0) {
            p = template + i;
            break;
        }
    }
    size_t before, after_off, after;
    if (p) {
        before = (size_t)(p - template);
        after_off = before + marker_len;
        after = template_size - after_off;
    } else {
        ywarn("splice_marker: '%s' not found, appending replacement at end", marker);
        before = template_size;
        after_off = template_size;
        after = 0;
    }
    size_t total = before + replacement_size + after;
    char *out = malloc(total + 1);
    if (!out) {
        return NULL;
    }
    memcpy(out, template, before);
    memcpy(out + before, replacement, replacement_size);
    memcpy(out + before + replacement_size, template + after_off, after);
    out[total] = 0;
    *out_size = total;
    return out;
}

/* Forward declarations */
static struct yetty_ycore_void_result shader_glyph_destroy(
    struct yetty_yrender_terminal_layer *self);
static struct yetty_ycore_void_result shader_glyph_process_input(
    struct yetty_yrender_terminal_layer *self,
    struct yetty_ywire_wire_statemachine *osc_statemachine);
static struct yetty_ycore_void_result shader_glyph_resize_grid(
    struct yetty_yrender_terminal_layer *self, struct yetty_ycore_grid_size grid_size,
    struct yetty_ycore_pixel_size cell_size);
static struct yetty_ycore_void_result shader_glyph_set_visual_zoom(
    struct yetty_yrender_terminal_layer *self, float scale, float off_x, float off_y);
static struct yetty_yrender_gpu_resource_set_result shader_glyph_get_gpu_resource_set(
    const struct yetty_yrender_terminal_layer *self);
static struct yetty_ycore_int_result shader_glyph_render(struct yetty_yrender_terminal_layer *self,
                                                         struct yetty_ydraw_target *target,
                                                         int force);
static struct yetty_ycore_int_result on_anim_tick(struct yetty_yevent_event_listener *listener,
                                                  const struct yetty_yui_event *event);
static void anim_timer_stop(struct yetty_yterm_shader_glyph_layer *layer);
static int shader_glyph_is_empty(const struct yetty_yrender_terminal_layer *self);
static int shader_glyph_is_dirty(const struct yetty_yrender_terminal_layer *self);
static int shader_glyph_on_key(struct yetty_yrender_terminal_layer *self, int key, int mods);
static int shader_glyph_on_char(struct yetty_yrender_terminal_layer *self, uint32_t codepoint,
                                int mods);
static struct yetty_ycore_void_result shader_glyph_scroll(struct yetty_yrender_terminal_layer *self,
                                                          int lines);
static struct yetty_ycore_void_result shader_glyph_set_cursor(
    struct yetty_yrender_terminal_layer *self, int col, int row);

static const struct yetty_yterm_terminal_layer_ops shader_glyph_layer_ops = {
    .destroy = shader_glyph_destroy,
    .process_input = shader_glyph_process_input,
    .resize_grid = shader_glyph_resize_grid,
    .set_visual_zoom = shader_glyph_set_visual_zoom,
    .get_gpu_resource_set = shader_glyph_get_gpu_resource_set,
    .render = shader_glyph_render,
    .is_dirty = shader_glyph_is_dirty,
    .is_empty = shader_glyph_is_empty,
    .on_key = shader_glyph_on_key,
    .on_char = shader_glyph_on_char,
    .scroll = shader_glyph_scroll,
    .set_cursor = shader_glyph_set_cursor,
};

struct yetty_yterm_terminal_layer_result yetty_yterm_shader_glyph_layer_create(
    uint32_t cols, uint32_t rows, float cell_width, float cell_height,
    struct yetty_yrender_terminal_layer *text_layer, const struct yetty_context *context,
    yetty_yterm_request_render_fn request_render_fn, void *request_render_userdata,
    yetty_yterm_scroll_fn scroll_fn, void *scroll_userdata, yetty_yterm_cursor_fn cursor_fn,
    void *cursor_userdata)
{
    if (!text_layer) {
        return YETTY_ERR(yetty_yterm_terminal_layer, "shader-glyph-layer: text_layer is NULL");
    }
    if (!context) {
        return YETTY_ERR(yetty_yterm_terminal_layer, "shader-glyph-layer: context is NULL");
    }

    /* Load shader template from disk (matches text-layer / ydraw-layer pattern). */
    struct yetty_yconfig_config *config = context->app_context.config;
    const char *shaders_dir = config->ops->get_string(config, "paths/shaders", "");
    char shader_path[512];
    char glyph_dir[512];
    snprintf(shader_path, sizeof(shader_path), "%s/shader-glyph-layer.wgsl", shaders_dir);
    snprintf(glyph_dir, sizeof(glyph_dir), "%s/glyph-shaders", shaders_dir);

    struct yetty_ycore_buffer_result template_res = yetty_ycore_read_file(shader_path);
    if (YETTY_IS_ERR(template_res)) {
        return YETTY_ERR(yetty_yterm_terminal_layer,
                         "shader_glyph_layer_create: read_file(shader-glyph-layer.wgsl) failed",
                         template_res);
    }

    /* Assemble per-glyph .wgsl files + generated dispatcher. The config
     * filters which glyphs get baked into the merged shader (see
     * shaders/preload/glyphs in defaults.yaml); the heavy "scene"
     * shaders like 0xeff5-butterfly-flock are commented out by default
     * because they hang the GPU on some integrated stacks. */
    struct yetty_ycore_buffer_result glyph_res = assemble_glyph_shaders(glyph_dir, config);
    if (YETTY_IS_ERR(glyph_res)) {
        free(template_res.value.data);
        return YETTY_ERR(yetty_yterm_terminal_layer, "shader-glyph-layer: assemble failed",
                         glyph_res);
    }
    char *glyph_blob = (char *)glyph_res.value.data;
    size_t glyph_size = glyph_res.value.size;

    /* Splice the assembled blob into the template's marker. */
    size_t spliced_size = 0;
    char *spliced =
        splice_marker((const char *)template_res.value.data, template_res.value.size,
                      "// SHADER_GLYPHS_PLACEHOLDER", glyph_blob, glyph_size, &spliced_size);
    free(template_res.value.data);
    free(glyph_blob);
    if (!spliced) {
        return YETTY_ERR(yetty_yterm_terminal_layer, "shader-glyph-layer: splice failed");
    }

    struct yetty_yterm_shader_glyph_layer *layer =
        calloc(1, sizeof(struct yetty_yterm_shader_glyph_layer));
    if (!layer) {
        free(spliced);
        return YETTY_ERR(yetty_yterm_terminal_layer, "shader-glyph-layer: alloc failed");
    }
    layer->shader_source = spliced;
    layer->shader_source_size = spliced_size;
    layer->text_layer = text_layer;

    layer->base.ops = &shader_glyph_layer_ops;
    layer->base.grid_size.cols = cols;
    layer->base.grid_size.rows = rows;
    layer->base.cell_size.width = cell_width;
    layer->base.cell_size.height = cell_height;
    /* Start dirty so the first frame uploads the buffer pointer + uniforms. */
    layer->base.dirty = 1;
    layer->base.pty_write_fn = NULL; /* not a PTY sink */
    layer->base.pty_write_userdata = NULL;
    layer->base.request_render_fn = request_render_fn;
    layer->base.request_render_userdata = request_render_userdata;
    layer->base.scroll_fn = scroll_fn;
    layer->base.scroll_userdata = scroll_userdata;
    layer->base.cursor_fn = cursor_fn;
    layer->base.cursor_userdata = cursor_userdata;

    clock_gettime(CLOCK_MONOTONIC, &layer->t0);

    /* Resource set */
    strncpy(layer->rs.namespace, "shader_glyph", YETTY_YRENDER_NAME_MAX - 1);

    /* Two read-only storage buffers:
     *   buffers[0] "cells"      : full cell buffer (same as text-layer's),
     *                              needed by the fragment shader for fg/bg
     *                              colours of each rendered cell.
     *   buffers[1] "instances"  : packed list of (cell_index, local_id) for
     *                              every shader-glyph cell visible this frame.
     *                              The vertex shader fetches one entry per
     *                              draw instance and positions a quad over
     *                              that cell, so the fragment shader only
     *                              fires on shader-glyph pixels. */
    layer->rs.buffer_count = 2;
    strncpy(layer->rs.buffers[0].name, "cells", YETTY_YRENDER_NAME_MAX - 1);
    strncpy(layer->rs.buffers[0].wgsl_type, "array<u32>", YETTY_YRENDER_WGSL_TYPE_MAX - 1);
    layer->rs.buffers[0].readonly = 1;
    strncpy(layer->rs.buffers[1].name, "instances", YETTY_YRENDER_NAME_MAX - 1);
    strncpy(layer->rs.buffers[1].wgsl_type, "array<u32>", YETTY_YRENDER_WGSL_TYPE_MAX - 1);
    layer->rs.buffers[1].readonly = 1;

    init_uniforms(&layer->rs);
    set_grid_size(&layer->rs, (float)cols, (float)rows);
    set_cell_size(&layer->rs, cell_width, cell_height);

    layer->rs.pixel_size.width = (float)cols * cell_width;
    layer->rs.pixel_size.height = (float)rows * cell_height;

    yetty_yrender_shader_code_set(&layer->rs.shader, layer->shader_source,
                                  layer->shader_source_size);

    /* Initial buffer pointer — refreshed each frame in get_gpu_resource_set. */
    const uint8_t *cells_data = NULL;
    size_t cells_size = 0;
    yetty_yterm_terminal_layer_terminal_text_layer_get_cells(text_layer, &cells_data, &cells_size);
    layer->rs.buffers[0].data = (uint8_t *)cells_data;
    layer->rs.buffers[0].size = cells_size;
    layer->rs.buffers[0].dirty = 1;

    /* Animation timer. Period from terminal/shader-glyph-layer/target-fps
     * (default 60). Created stopped — render() starts/stops it based on
     * is_empty so empty terminals cost nothing. */
    layer->event_loop = context->event_loop;
    layer->listener.handler = on_anim_tick;
    if (layer->event_loop && layer->event_loop->ops && layer->event_loop->ops->create_timer) {
        int target_fps = config->ops->get_int(config, "terminal/shader-glyph-layer/target-fps", 60);
        if (target_fps < 1) {
            target_fps = 1;
        }
        if (target_fps > 1000) {
            target_fps = 1000;
        }
        int period_ms = 1000 / target_fps;
        if (period_ms < 1) {
            period_ms = 1;
        }

        struct yetty_yevent_timer_id_result tres =
            layer->event_loop->ops->create_timer(layer->event_loop);
        if (YETTY_IS_OK(tres)) {
            layer->timer_id = tres.value;
            layer->event_loop->ops->config_timer(layer->event_loop, layer->timer_id, period_ms);
            layer->event_loop->ops->register_timer_listener(layer->event_loop, layer->timer_id,
                                                            &layer->listener);
            layer->timer_created = 1;
            yinfo("shader-glyph-layer: anim timer fps=%d period=%dms", target_fps, period_ms);
        } else {
            ywarn("shader-glyph-layer: create_timer failed; animations static");
        }
    }

    ydebug("shader_glyph_layer_create: %ux%u grid, %.1fx%.1f cells", cols, rows, cell_width,
           cell_height);

    return YETTY_OK(yetty_yterm_terminal_layer, &layer->base);
}

static struct yetty_ycore_void_result shader_glyph_destroy(
    struct yetty_yrender_terminal_layer *self)
{
    struct yetty_yterm_shader_glyph_layer *layer =
        container_of(self, struct yetty_yterm_shader_glyph_layer, base);
    if (layer->timer_created && layer->event_loop && layer->event_loop->ops) {
        if (layer->timer_running && layer->event_loop->ops->stop_timer) {
            layer->event_loop->ops->stop_timer(layer->event_loop, layer->timer_id);
        }
        if (layer->event_loop->ops->destroy_timer) {
            layer->event_loop->ops->destroy_timer(layer->event_loop, layer->timer_id);
        }
    }
    free(layer->shader_source);
    free(layer->instances);
    free(layer);
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result shader_glyph_process_input(
    struct yetty_yrender_terminal_layer *self,
    struct yetty_ywire_wire_statemachine *osc_statemachine)
{
    (void)self;
    (void)osc_statemachine;
    /* Passive consumer of the text grid; not registered with the SM. */
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result shader_glyph_resize_grid(
    struct yetty_yrender_terminal_layer *self, struct yetty_ycore_grid_size grid_size,
    struct yetty_ycore_pixel_size cell_size)
{
    struct yetty_yterm_shader_glyph_layer *layer =
        container_of(self, struct yetty_yterm_shader_glyph_layer, base);

    if (cell_size.width <= 0.0f || cell_size.height <= 0.0f) {
        return YETTY_ERR(yetty_ycore_void, "invalid cell size");
    }

    self->grid_size = grid_size;
    self->cell_size = cell_size;
    set_grid_size(&layer->rs, (float)grid_size.cols, (float)grid_size.rows);
    set_cell_size(&layer->rs, cell_size.width, cell_size.height);
    layer->rs.pixel_size.width = (float)grid_size.cols * cell_size.width;
    layer->rs.pixel_size.height = (float)grid_size.rows * cell_size.height;
    self->dirty = 1;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result shader_glyph_set_visual_zoom(
    struct yetty_yrender_terminal_layer *self, float scale, float off_x, float off_y)
{
    struct yetty_yterm_shader_glyph_layer *layer =
        container_of(self, struct yetty_yterm_shader_glyph_layer, base);
    set_visual_zoom(&layer->rs, scale, off_x, off_y);
    self->dirty = 1;
    return YETTY_OK_VOID();
}

static struct yetty_yrender_gpu_resource_set_result shader_glyph_get_gpu_resource_set(
    const struct yetty_yrender_terminal_layer *self)
{
    struct yetty_yterm_shader_glyph_layer *layer = container_of(
        (struct yetty_yrender_terminal_layer *)self, struct yetty_yterm_shader_glyph_layer, base);

    /* Refresh the cell buffer pointer — text-layer may switch between live
     * vterm screen and stitched scrollback view between frames. */
    const uint8_t *cells_data = NULL;
    size_t cells_size = 0;
    yetty_yterm_terminal_layer_terminal_text_layer_get_cells(layer->text_layer, &cells_data,
                                                             &cells_size);
    if ((const uint8_t *)layer->rs.buffers[0].data != cells_data ||
        layer->rs.buffers[0].size != cells_size) {
        layer->rs.buffers[0].data = (uint8_t *)cells_data;
        layer->rs.buffers[0].size = cells_size;
    }
    layer->rs.buffers[0].dirty = 1;

    /* Build the per-cell instance list. One pass over the cell buffer
     * extracting (cell_index, local_id) for every cell whose glyph_index
     * has bit-31 set. The vertex shader will fetch this and emit a quad
     * over exactly that cell — so fragment shader fires only on shader-
     * glyph pixels rather than every pixel of the pane. */
    uint32_t cols = (uint32_t)layer->base.grid_size.cols;
    uint32_t rows = (uint32_t)layer->base.grid_size.rows;
    size_t live_cells = (size_t)cols * (size_t)rows;
    /* vterm allocates 2*rows tall — only scan the live half (top rows×cols).
     * The bottom half is unused for the visible screen. */
    if (cells_size < live_cells * 12u) {
        live_cells = cells_size / 12u;
    }
    const uint32_t *p = (const uint32_t *)cells_data;
    /* Grow instances[] on demand. Worst case = grid area, but typical = a
     * handful of cells, so this realloc is rare after warm-up. */
    if (layer->instance_cap < live_cells) {
        struct shader_glyph_instance *resized =
            realloc(layer->instances, live_cells * sizeof(*resized));
        if (resized) {
            layer->instances = resized;
            layer->instance_cap = live_cells;
        }
    }
    uint32_t n = 0;
    if (layer->instances) {
        for (size_t i = 0; i < live_cells; i++) {
            uint32_t g = p[i * 3u];
            if (g >> 31) {
                layer->instances[n].cell_index = (uint32_t)i;
                /* local_id = 0xFFFFFFFF - glyph_index (matches
                 * yetty_shader_glyph_id_from_local). */
                layer->instances[n].local_id = 0xFFFFFFFFu - g;
                n++;
            }
        }
    }
    layer->instance_count = n;

    /* buffers[1] = instance list */
    layer->rs.buffers[1].data = (uint8_t *)layer->instances;
    layer->rs.buffers[1].size = (size_t)n * sizeof(struct shader_glyph_instance);
    layer->rs.buffers[1].dirty = 1;

    /* Tell render-target how many instances to draw. Zero is fine — the
     * outer is_empty() returns 1 in that case and the layer is skipped
     * entirely; if we ever do get here with n=0 the draw becomes a no-op. */
    layer->rs.instance_count = n;

    ydebug("shader-glyph: get_gpu_resource_set: cols=%u rows=%u live_cells=%zu "
           "instances=%u cells_size=%zu instances_size=%zu",
           cols, rows, live_cells, n, cells_size,
           (size_t)n * sizeof(struct shader_glyph_instance));
    if (n > 0 && n <= 4) {
        for (uint32_t k = 0; k < n; k++) {
            ydebug("shader-glyph: inst[%u] cell_index=%u local_id=%u", k,
                   layer->instances[k].cell_index, layer->instances[k].local_id);
        }
    } else if (n > 4) {
        ydebug("shader-glyph: inst[0]=(cell=%u,local=%u) inst[%u]=(cell=%u,local=%u)",
               layer->instances[0].cell_index, layer->instances[0].local_id,
               n - 1, layer->instances[n - 1].cell_index, layer->instances[n - 1].local_id);
    }

    /* Update animation clock. */
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    float t =
        (float)(now.tv_sec - layer->t0.tv_sec) + (float)(now.tv_nsec - layer->t0.tv_nsec) * 1e-9f;
    set_time(&layer->rs, t);

    return YETTY_OK(yetty_yrender_gpu_resource_set, &layer->rs);
}

/* Recover the layer pointer from its embedded tick listener. */
static inline struct yetty_yterm_shader_glyph_layer *layer_from_listener(
    struct yetty_yevent_event_listener *l)
{
    return (struct yetty_yterm_shader_glyph_layer *)((char *)l -
                                                     offsetof(struct yetty_yterm_shader_glyph_layer,
                                                              listener));
}

/* Animation tick — runs on the event-loop thread at target_fps. Schedules
 * one render; the actual draw happens when RENDER is dispatched. Returns 0
 * (not-handled) so the timer event still propagates to other listeners.
 *
 * Self-stops when the layer has gone empty. terminal_render_frame skips the
 * layer's render() if is_empty() returns 1, so shader_glyph_render's stop
 * path never runs once the last shader-glyph cell disappears — the timer
 * would otherwise tick forever, re-firing request_render at 60 Hz and
 * pinning the GPU at 100% on idle terminals. Check from here so we stop
 * the timer regardless of whether render() ever gets called. */
static struct yetty_ycore_int_result on_anim_tick(struct yetty_yevent_event_listener *listener,
                                                  const struct yetty_yui_event *event)
{
    (void)event;
    struct yetty_yterm_shader_glyph_layer *layer = layer_from_listener(listener);
    if (shader_glyph_is_empty(&layer->base)) {
        anim_timer_stop(layer);
        return YETTY_OK(yetty_ycore_int, 0);
    }
    /* Mark dirty BEFORE requesting the render. terminal_render_frame's new
     * dirty gate skips clean layers; without this the tick would request a
     * render where shader-glyph then gets skipped, advancing time without
     * actually re-painting the cells. */
    layer->base.dirty = 1;
    if (layer->base.request_render_fn) {
        struct yetty_ycore_void_result r =
            layer->base.request_render_fn(layer->base.request_render_userdata);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, r,
                            "on_anim_tick: request_render_fn failed");
    }
    return YETTY_OK(yetty_ycore_int, 0);
}

static void anim_timer_start(struct yetty_yterm_shader_glyph_layer *layer)
{
    if (!layer->timer_created || layer->timer_running) {
        return;
    }
    layer->event_loop->ops->start_timer(layer->event_loop, layer->timer_id);
    layer->timer_running = 1;
}

static void anim_timer_stop(struct yetty_yterm_shader_glyph_layer *layer)
{
    if (!layer->timer_created || !layer->timer_running) {
        return;
    }
    layer->event_loop->ops->stop_timer(layer->event_loop, layer->timer_id);
    layer->timer_running = 0;
}

static struct yetty_ycore_int_result shader_glyph_render(struct yetty_yrender_terminal_layer *self,
                                                         struct yetty_ydraw_target *target,
                                                         int force)
{
    struct yetty_yterm_shader_glyph_layer *layer =
        container_of(self, struct yetty_yterm_shader_glyph_layer, base);

    /* Animation timer gates only the animation tick — when there's nothing
     * on screen to animate, stop ticking so the input→render loop can
     * idle. Otherwise, ensure the timer is ticking at target_fps. */
    if (shader_glyph_is_empty(self)) {
        anim_timer_stop(layer);
        ydebug("shader-glyph: render skipped (empty)");
        return YETTY_OK(yetty_ycore_int, 0);
    }
    anim_timer_start(layer);
    /* Honour the cascade: even if our own dirty isn't set (no anim
     * tick this frame), if force=1 the big_target underneath was
     * just rewritten by a lower layer — repaint to keep our pixels.
     * Per-cell instanced draw fires on shader-glyph cells only, so
     * unchanged frames cost nothing on the GPU anyway. */
    if (!self->dirty && !force) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    ydebug("shader-glyph: render_layer ENTER instance_count=%u", layer->instance_count);
    struct yetty_ycore_void_result r = target->ops->render_layer(target, self);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, r, "shader_glyph_render: target->render_layer");
    ydebug("shader-glyph: render_layer EXIT");
    return YETTY_OK(yetty_ycore_int, 1);
}

/* Empty when there are no shader-glyph cells in the current grid.
 *
 * This is what stops the continuous-render loop from consuming a GPU pass
 * every frame in idle terminals. Cheap CPU scan over the cell buffer (~100µs
 * for a 200k-cell grid; see poc/term-grid-scan). The render_target will skip
 * draws when is_empty returns 1. */
static int shader_glyph_is_empty(const struct yetty_yrender_terminal_layer *self)
{
    const struct yetty_yterm_shader_glyph_layer *layer = container_of(
        (struct yetty_yrender_terminal_layer *)self, struct yetty_yterm_shader_glyph_layer, base);

    const uint8_t *data = NULL;
    size_t size = 0;
    yetty_yterm_terminal_layer_terminal_text_layer_get_cells(layer->text_layer, &data, &size);
    if (!data || size < 12) {
        return 1;
    }

    /* Scalar branchless scan — the POC showed this matches AVX2 for
     * <200k cells and is bandwidth-bound at any size. Cell stride is 12B. */
    const uint32_t *p = (const uint32_t *)data;
    size_t cells = size / 12u;
    int found = 0;
    for (size_t i = 0; i < cells; i++) {
        uint32_t g = p[i * 3u];
        found |= (int)(g >> 31);
    }
    return found ? 0 : 1;
}

/* The anim timer sets self->dirty every tick while we have shader-glyph
 * cells on screen, so this is the only bit shader_glyph_render reads
 * (force aside). Empty grids report not-dirty so the timer can idle. */
static int shader_glyph_is_dirty(const struct yetty_yrender_terminal_layer *self)
{
    if (shader_glyph_is_empty(self)) {
        return 0;
    }
    return self->dirty;
}

static int shader_glyph_on_key(struct yetty_yrender_terminal_layer *self, int key, int mods)
{
    (void)self;
    (void)key;
    (void)mods;
    return 0;
}

static int shader_glyph_on_char(struct yetty_yrender_terminal_layer *self, uint32_t codepoint,
                                int mods)
{
    (void)self;
    (void)codepoint;
    (void)mods;
    return 0;
}

static struct yetty_ycore_void_result shader_glyph_scroll(struct yetty_yrender_terminal_layer *self,
                                                          int lines)
{
    (void)lines;
    self->dirty = 1;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result shader_glyph_set_cursor(
    struct yetty_yrender_terminal_layer *self, int col, int row)
{
    (void)self;
    (void)col;
    (void)row;
    return YETTY_OK_VOID();
}
