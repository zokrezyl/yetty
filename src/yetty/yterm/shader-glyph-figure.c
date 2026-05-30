/*
 * shader-glyph-figure.c — animated procedural glyphs rendered as a
 * yfigure_figure subclass.
 *
 * One figure per terminal, added as a child of the root container by
 * terminal_create. Each frame the figure scans the borrowed text-layer
 * cell buffer for cells whose glyph_index has bit-31 set, packs the per-
 * instance (cell_index, local_id) list, and issues a single instanced
 * draw — same GPU shape the layer version had, with the surrounding
 * pipeline replaced by self-owned binder + render pass.
 *
 * Lifetime: created in terminal_create after the root container, attached
 * via yetty_yfigure_container_add_child under a reserved id. The container
 * destroys it during teardown.
 *
 * Animation timer: owned here, registered on the borrowed event loop.
 * Self-stops when is_empty() — idle terminals cost nothing. The tick
 * sets self->dirty = 1 and calls request_render_fn; the next render frame
 * lands here through the container's render walk.
 */
#include <yetty/yplatform/compat.h> /* clock_gettime shim on MSVC */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <webgpu/webgpu.h>

#include <yetty/yconfig/config.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/util.h>
#include <yetty/yetty/yetty.h>
#include <yetty/yevent/event-loop.h>
#include <yetty/yevent/event.h>
#include <yetty/yfigure/figure.h>
#include <yetty/yframework/yframework.h>
#include <yetty/yplatform/fs.h>
#include <yetty/yrender/gpu-resource-binder.h>
#include <yetty/yrender/gpu-resource-set.h>
#include <yetty/yrender/render-target.h>
#include <yetty/yterm/shader-glyph-figure.h>
#include <yetty/yterm/text-layer.h>
#include <yetty/ytrace/ytrace.h>

/* Uniform slots */
#define U_GRID_SIZE 0
#define U_CELL_SIZE 1
#define U_TIME 2
#define U_VZ_SCALE 3
#define U_VZ_OFF 4
#define U_COUNT 5

/* Per-instance entry uploaded to buffers[1]. One entry per shader-glyph
 * cell visible this frame; the vertex shader fetches it by instance_index,
 * positions a quad over the cell, and passes local_id + colors through to
 * the fragment shader. Keeping it tiny (8 bytes) keeps the upload cheap. */
struct shader_glyph_instance {
    uint32_t cell_index; /* row * cols + col within the cell buffer */
    uint32_t local_id;   /* index into the generated render_shader_glyph dispatcher */
};

struct [[clang::annotate("class@yterm:shader_glyph")]] [[clang::annotate("parent@yfigure:figure")]]
yetty_yterm_shader_glyph_figure {
    struct yetty_yfigure_figure base;

    /* Borrowed — text-layer owns the cell buffer; we just point at it. */
    struct yetty_yrender_terminal_layer *text_layer;

    /* Borrowed. Held to mint the binder on first render and to ask for
     * future renders from the anim timer. */
    const struct yetty_context *context;
    yetty_yterm_request_render_fn request_render_fn;
    void *request_render_userdata;

    /* Grid + cell geometry. Mirrored in rs.uniforms; tracked separately
     * so resize() doesn't have to plumb through the uniform array. */
    struct yetty_ycore_grid_size grid_size;
    struct yetty_ycore_pixel_size cell_size;

    /* Final assembled shader source (template with glyph code spliced in). */
    char *shader_source;
    size_t shader_source_size;

    /* GPU plumbing. The binder owns the pipeline + bind groups + flat
     * buffers; it's compiled lazily on first render so creating the
     * figure doesn't fail when called before the GPU is fully ready
     * (matches yrdawn_figure's lazy build_pipeline pattern). */
    struct yetty_ydraw_gpu_resource_set rs;
    struct yetty_yrender_gpu_resource_binder *binder;
    int binder_finalized;

    /* CPU-side animation clock origin. Time uniform is (now - t0). */
    struct timespec t0;

    /* Per-frame cell instance list. rs.buffers[1] points here. */
    struct shader_glyph_instance *instances;
    size_t instance_cap;
    uint32_t instance_count;

    /* Animation timer. Borrowed event loop; timer_id valid only when
     * timer_created. */
    struct yetty_yevent_event_loop *event_loop;
    yetty_yevent_timer_id timer_id;
    int timer_created;
    int timer_running;
    struct yetty_yevent_event_listener listener;
};

/* ===========================================================================
 * Uniform helpers
 * ========================================================================= */

static inline void set_grid_size(struct yetty_ydraw_gpu_resource_set *rs, float cols, float rows)
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

/* ===========================================================================
 * glyph-shader assembly
 *
 * At create time we scan `<shaders_dir>/glyph-shaders/0xNNNN-*.wgsl`, read
 * each file, sort by local_id, concatenate all bodies, then emit a generated
 * `render_shader_glyph(local_id, ...)` switch dispatcher. The result is
 * spliced into shader-glyph-layer.wgsl at the `// SHADER_GLYPHS_PLACEHOLDER`
 * marker. No build-time codegen — adding a glyph is just dropping a .wgsl
 * file into the directory and relaunching.
 * ========================================================================= */

struct glyph_entry {
    uint32_t local_id;
    char *body;
    size_t body_size;
};

static int glyph_entry_cmp(const void *a, const void *b)
{
    uint32_t la = ((const struct glyph_entry *)a)->local_id;
    uint32_t lb = ((const struct glyph_entry *)b)->local_id;
    return (la > lb) - (la < lb);
}

/* Match a glyph file's basename (e.g. "0x0000-spinner.wgsl") against an
 * entry in shaders/preload/glyphs (e.g. "0x0000-spinner"). Compares case-
 * insensitively because the YAML config tends to have "0xeff5-..." in
 * lowercase while filenames vary, and the hex prefix is the only part
 * that risks case skew. */
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

/* Strict allow-list — missing key, empty list, or NULL config → load
 * NOTHING. The user must explicitly enumerate which glyphs to compile in.
 * Avoids dragging in heavy shaders (butterfly-flock, hg-sdf, etc.) that
 * hang the GPU on Intel HD 530 / Mesa-ANV when they're merely included
 * in the merged WGSL even without runtime invocation. */
static int glyph_allowed(struct yetty_yconfig_config *config, const char *filename)
{
    if (!config) {
        return 0;
    }
    int n = config->ops->get_array_count(config, "shaders/preload/glyphs");
    for (int i = 0; i < n; i++) {
        const char *want = config->ops->get_array_item(config, "shaders/preload/glyphs", i, NULL);
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
 * util_hash, util_valueNoise, util_colorNoise). They are concatenated FIRST,
 * in name-sorted order, so glyph functions can call their helpers. Files
 * starting with '0x' are glyphs, sorted by local-id — filtered through
 * shaders/preload/glyphs in the user's yetty config. */
static struct yetty_ycore_buffer_result assemble_glyph_shaders(const char *glyph_dir,
                                                               struct yetty_yconfig_config *config)
{
    struct yetty_yplatform_dir *d = yetty_yplatform_dir_open(glyph_dir);
    if (!d) {
        ywarn("glyph-shaders: open(%s) failed", glyph_dir);
        return YETTY_ERR(yetty_ycore_buffer, "glyph-shaders: dir open failed");
    }
    int allow_list_size =
        config ? config->ops->get_array_count(config, "shaders/preload/glyphs") : 0;
    yinfo("glyph-shaders: explicit allow-list, %d glyphs requested", allow_list_size);

    struct glyph_entry *entries = NULL;
    size_t cap = 0, n = 0;

    struct yetty_ycore_buffer prelude_bufs[8];
    char prelude_names[8][128];
    size_t prelude_count = 0;

    struct yetty_yplatform_dir_entry de;
    while (yetty_yplatform_dir_next(d, &de)) {
        const char *name = de.name;
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
            struct glyph_entry *grown = realloc(entries, new_cap * sizeof(*entries));
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
    yetty_yplatform_dir_close(d);

    qsort(entries, n, sizeof(struct glyph_entry), glyph_entry_cmp);

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

/* Substitute the first occurrence of `marker` in `template` with
 * `replacement`. Returns malloc'd buffer; caller frees. *out_size set to
 * result length. */
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

/* ===========================================================================
 * Cell scan helpers
 * ========================================================================= */

/* Cheap branchless scan over the cell buffer (12B stride). Returns 1 iff
 * at least one cell has glyph_index with bit-31 set. Used to short-circuit
 * the anim timer when the grid is idle. */
static int figure_is_empty(struct yetty_yterm_shader_glyph_figure *f)
{
    const uint8_t *data = NULL;
    size_t size = 0;
    yetty_yterm_terminal_layer_terminal_text_layer_get_cells(f->text_layer, &data, &size);
    if (!data || size < 12) {
        return 1;
    }
    const uint32_t *p = (const uint32_t *)data;
    size_t cells = size / 12u;
    int found = 0;
    for (size_t i = 0; i < cells; i++) {
        uint32_t g = p[i * 3u];
        found |= (int)(g >> 31);
    }
    return found ? 0 : 1;
}

/* Walk the cell buffer once, packing (cell_index, local_id) per shader-
 * glyph cell into f->instances[]. Returns the instance count. */
static uint32_t figure_pack_instances(struct yetty_yterm_shader_glyph_figure *f)
{
    const uint8_t *cells_data = NULL;
    size_t cells_size = 0;
    yetty_yterm_terminal_layer_terminal_text_layer_get_cells(f->text_layer, &cells_data,
                                                             &cells_size);
    if (!cells_data || cells_size < 12) {
        f->instance_count = 0;
        return 0;
    }

    /* Keep the live-cell window honest: vterm allocates 2*rows tall but
     * only the top rows×cols are on-screen. Bound the scan accordingly. */
    uint32_t cols = (uint32_t)f->grid_size.cols;
    uint32_t rows = (uint32_t)f->grid_size.rows;
    size_t live_cells = (size_t)cols * (size_t)rows;
    if (cells_size < live_cells * 12u) {
        live_cells = cells_size / 12u;
    }

    if (f->instance_cap < live_cells) {
        struct shader_glyph_instance *resized =
            realloc(f->instances, live_cells * sizeof(*resized));
        if (resized) {
            f->instances = resized;
            f->instance_cap = live_cells;
        }
    }
    uint32_t n = 0;
    if (f->instances) {
        const uint32_t *p = (const uint32_t *)cells_data;
        for (size_t i = 0; i < live_cells; i++) {
            uint32_t g = p[i * 3u];
            if (g >> 31) {
                f->instances[n].cell_index = (uint32_t)i;
                f->instances[n].local_id = 0xFFFFFFFFu - g;
                n++;
            }
        }
    }

    /* Re-point the storage-buffer descriptor at the freshly-packed list
     * and re-point the cell buffer (text-layer may switch between live
     * vterm screen and stitched scrollback between frames). */
    f->rs.buffers[0].data = (uint8_t *)cells_data;
    f->rs.buffers[0].size = cells_size;
    f->rs.buffers[0].dirty = 1;
    f->rs.buffers[1].data = (uint8_t *)f->instances;
    f->rs.buffers[1].size = (size_t)n * sizeof(struct shader_glyph_instance);
    f->rs.buffers[1].dirty = 1;
    f->rs.instance_count = n;
    f->instance_count = n;
    return n;
}

/* ===========================================================================
 * Animation timer
 * ========================================================================= */

static inline struct yetty_yterm_shader_glyph_figure *figure_from_listener(
    struct yetty_yevent_event_listener *l)
{
    return (
        struct yetty_yterm_shader_glyph_figure *)((char *)l -
                                                  offsetof(struct yetty_yterm_shader_glyph_figure,
                                                           listener));
}

static void anim_timer_stop(struct yetty_yterm_shader_glyph_figure *f)
{
    if (!f->timer_created || !f->timer_running) {
        return;
    }
    f->event_loop->ops->stop_timer(f->event_loop, f->timer_id);
    f->timer_running = 0;
}

static void anim_timer_start(struct yetty_yterm_shader_glyph_figure *f)
{
    if (!f->timer_created || f->timer_running) {
        return;
    }
    f->event_loop->ops->start_timer(f->event_loop, f->timer_id);
    f->timer_running = 1;
}

/* Anim tick — runs on the event-loop thread at target_fps. Self-stops
 * when the grid has gone empty so idle terminals quiesce. Sets the
 * figure dirty bit BEFORE requesting render so the next frame actually
 * repaints (render() bails on !dirty). */
static struct yetty_ycore_int_result on_anim_tick(struct yetty_yevent_event_listener *listener,
                                                  const struct yetty_yui_event *event)
{
    (void)event;
    struct yetty_yterm_shader_glyph_figure *f = figure_from_listener(listener);
    if (figure_is_empty(f)) {
        anim_timer_stop(f);
        return YETTY_OK(yetty_ycore_int, 0);
    }
    f->base.dirty = 1;
    if (f->request_render_fn) {
        struct yetty_ycore_void_result r = f->request_render_fn(f->request_render_userdata);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, r, "shader-glyph anim tick: request_render failed");
    }
    return YETTY_OK(yetty_ycore_int, 0);
}

/* ===========================================================================
 * Figure ops — destroy
 * ========================================================================= */

static struct yetty_ycore_void_result figure_destroy(struct yetty_yfigure_figure *self)
{
    if (!self) {
        return YETTY_OK_VOID();
    }
    struct yetty_yterm_shader_glyph_figure *f = (struct yetty_yterm_shader_glyph_figure *)self;

    if (f->timer_created && f->event_loop && f->event_loop->ops) {
        if (f->timer_running && f->event_loop->ops->stop_timer) {
            f->event_loop->ops->stop_timer(f->event_loop, f->timer_id);
        }
        if (f->event_loop->ops->destroy_timer) {
            f->event_loop->ops->destroy_timer(f->event_loop, f->timer_id);
        }
    }
    if (f->binder) {
        f->binder->ops->destroy(f->binder);
        f->binder = NULL;
    }
    free(f->shader_source);
    free(f->instances);
    /* Free the yclass allocation (header + body); body began at obj + 1. */
    return yetty_yclass_object_free((struct yetty_yclass_object *)self - 1);
}

/* ===========================================================================
 * Figure ops — render
 *
 * Lazy-builds the binder on first call (so create can run before the GPU
 * is fully wired up), then replicates the encode/draw loop from
 * render-target-texture.c::render_layer using its own binder, since the
 * binder API is keyed to a resource_set rather than a layer pointer.
 * ========================================================================= */

static struct yetty_ycore_void_result figure_ensure_binder(
    struct yetty_yterm_shader_glyph_figure *f)
{
    if (f->binder) {
        return YETTY_OK_VOID();
    }
    struct yetty_yframework_gpu_context *gpu = &f->context->runtime->gpu;
    struct yetty_yrender_gpu_resource_binder_result br = yetty_yrender_gpu_resource_binder_create(
        gpu->device, gpu->queue, gpu->surface_format, gpu->allocator);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, br, "shader-glyph figure: binder create failed");
    f->binder = br.value;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result figure_render(struct yetty_yfigure_figure *self,
                                                    struct yetty_ydraw_target *target)
{
    struct yetty_yterm_shader_glyph_figure *f = (struct yetty_yterm_shader_glyph_figure *)self;

    /* No shader-glyph cells visible → stop ticking, skip the draw.
     * Without this the layer would burn a render pass every frame for an
     * empty grid. */
    if (figure_is_empty(f)) {
        anim_timer_stop(f);
        return YETTY_OK_VOID();
    }
    anim_timer_start(f);

    /* Bring the binder up the first time we actually have a frame to
     * draw. The figure may be created before GPU init in test harnesses;
     * defer the dependency until it actually matters. */
    struct yetty_ycore_void_result ebr = figure_ensure_binder(f);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, ebr, "shader-glyph figure: ensure_binder");

    /* Refresh the per-frame resource set: cell buffer pointer, packed
     * instance list, time uniform. */
    (void)figure_pack_instances(f);

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    float t = (float)(now.tv_sec - f->t0.tv_sec) + (float)(now.tv_nsec - f->t0.tv_nsec) * 1e-9f;
    set_time(&f->rs, t);

    /* Push to binder: submit collects buffer/uniform layout, finalize
     * compiles the shader on first call, update queues uniform/buffer
     * writes onto the queue. */
    struct yetty_ycore_void_result sr = f->binder->ops->submit(f->binder, &f->rs);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "shader-glyph figure: binder submit");
    if (!f->binder_finalized) {
        struct yetty_ycore_void_result fr = f->binder->ops->finalize(f->binder);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, fr, "shader-glyph figure: binder finalize");
        f->binder_finalized = 1;
    }
    struct yetty_ycore_void_result ur = f->binder->ops->update(f->binder);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, ur, "shader-glyph figure: binder update");

    WGPUTextureView view = target->ops->get_view(target);
    if (!view) {
        ywarn("shader-glyph figure: render target has no view");
        return YETTY_OK_VOID();
    }
    WGPURenderPipeline pipeline = f->binder->ops->get_pipeline(f->binder);
    WGPUBuffer quad_vb = f->binder->ops->get_quad_vertex_buffer(f->binder);
    if (!pipeline || !quad_vb) {
        return YETTY_OK_VOID();
    }

    struct yetty_yframework_gpu_context *gpu = &f->context->runtime->gpu;
    WGPUCommandEncoderDescriptor enc_desc = {0};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(gpu->device, &enc_desc);
    if (!encoder) {
        return YETTY_ERR(yetty_ycore_void, "shader-glyph figure: command encoder create failed");
    }

    /* LoadOp_Load → preserve whatever lower layers (text-layer etc.)
     * already painted. The figure paints shader-glyph quads on top. */
    WGPURenderPassColorAttachment ca = {0};
    ca.view = view;
    ca.loadOp = WGPULoadOp_Load;
    ca.storeOp = WGPUStoreOp_Store;
    ca.clearValue = (WGPUColor){0.0, 0.0, 0.0, 0.0};
    ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;

    WGPURenderPassDescriptor pass_desc = {0};
    pass_desc.colorAttachmentCount = 1;
    pass_desc.colorAttachments = &ca;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &pass_desc);
    if (!pass) {
        wgpuCommandEncoderRelease(encoder);
        return YETTY_ERR(yetty_ycore_void, "shader-glyph figure: render pass begin failed");
    }

    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    f->binder->ops->bind(f->binder, pass, 0);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, quad_vb, 0, WGPU_WHOLE_SIZE);

    /* Confine to the target's pane viewport — same as render_layer.
     * The figure's rect IS the pane rect by construction, so we don't
     * need a separate clamp; use target->viewport directly. */
    struct yetty_yrender_viewport vp = target->viewport;
    wgpuRenderPassEncoderSetViewport(pass, vp.x, vp.y, vp.w, vp.h, 0.0f, 1.0f);
    wgpuRenderPassEncoderSetScissorRect(pass, (uint32_t)vp.x, (uint32_t)vp.y, (uint32_t)vp.w,
                                        (uint32_t)vp.h);

    uint32_t instance_count = f->rs.instance_count > 0 ? f->rs.instance_count : 1u;
    ydebug("shader-glyph figure: draw instances=%u", instance_count);
    wgpuRenderPassEncoderDraw(pass, 6, instance_count, 0, 0);

    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    WGPUCommandBufferDescriptor cb_desc = {0};
    WGPUCommandBuffer cb = wgpuCommandEncoderFinish(encoder, &cb_desc);
    wgpuQueueSubmit(gpu->queue, 1, &cb);
    wgpuCommandBufferRelease(cb);
    wgpuCommandEncoderRelease(encoder);
    return YETTY_OK_VOID();
}

/* ===========================================================================
 * Ops vtable
 * ========================================================================= */

/* yclass cross-domain override of yfigure:render. Body sits at obj + 1. */
[[clang::annotate("override@yterm:shader_glyph:yfigure:render")]]
static struct yetty_ycore_void_result figure_render_slot(struct yetty_yclass_ctx *ctx,
                                                         struct yetty_yclass_object *obj,
                                                         struct yetty_ydraw_target *target)
{
    (void)ctx;
    return figure_render((struct yetty_yfigure_figure *)(obj + 1), target);
}

/* yclass cross-domain override of yfigure:destroy. Body sits at obj + 1. */
[[clang::annotate("override@yterm:shader_glyph:yfigure:destroy")]]
static struct yetty_ycore_void_result figure_destroy_slot(struct yetty_yclass_ctx *ctx,
                                                          struct yetty_yclass_object *obj)
{
    (void)ctx;
    return figure_destroy((struct yetty_yfigure_figure *)(obj + 1));
}

static const struct yetty_yfigure_figure_ops *figure_ops(void)
{
    static const struct yetty_yfigure_figure_ops ops = {
        /* No process_input / process_bytes — purely visual, not wire-driven. */
        .process_input = NULL,
        .process_bytes = NULL,
    };
    return &ops;
}

/* ===========================================================================
 * Public API
 * ========================================================================= */

struct yetty_yterm_shader_glyph_figure_ptr_result yetty_yterm_shader_glyph_figure_create(
    struct yetty_ycore_rectangle rect, uint32_t cols, uint32_t rows, float cell_width,
    float cell_height, struct yetty_yrender_terminal_layer *text_layer,
    const struct yetty_context *context, yetty_yterm_request_render_fn request_render_fn,
    void *request_render_userdata)
{
    if (!text_layer) {
        return YETTY_ERR(yetty_yterm_shader_glyph_figure_ptr,
                         "shader-glyph figure: text_layer is NULL");
    }
    if (!context) {
        return YETTY_ERR(yetty_yterm_shader_glyph_figure_ptr,
                         "shader-glyph figure: context is NULL");
    }

    /* Load shader template + glyph dispatcher (matches old layer path). */
    struct yetty_yconfig_config *config = context->runtime->config;
    const char *shaders_dir = config->ops->get_string(config, "paths/shaders", "");
    char shader_path[512];
    char glyph_dir[512];
    snprintf(shader_path, sizeof(shader_path), "%s/shader-glyph-layer.wgsl", shaders_dir);
    snprintf(glyph_dir, sizeof(glyph_dir), "%s/glyph-shaders", shaders_dir);

    struct yetty_ycore_buffer_result template_res = yetty_ycore_read_file(shader_path);
    if (YETTY_IS_ERR(template_res)) {
        return YETTY_ERR(yetty_yterm_shader_glyph_figure_ptr,
                         "shader-glyph figure: read_file(shader-glyph-layer.wgsl) failed",
                         template_res);
    }

    struct yetty_ycore_buffer_result glyph_res = assemble_glyph_shaders(glyph_dir, config);
    if (YETTY_IS_ERR(glyph_res)) {
        free(template_res.value.data);
        return YETTY_ERR(yetty_yterm_shader_glyph_figure_ptr,
                         "shader-glyph figure: assemble failed", glyph_res);
    }
    char *glyph_blob = (char *)glyph_res.value.data;
    size_t glyph_size = glyph_res.value.size;

    size_t spliced_size = 0;
    char *spliced =
        splice_marker((const char *)template_res.value.data, template_res.value.size,
                      "// SHADER_GLYPHS_PLACEHOLDER", glyph_blob, glyph_size, &spliced_size);
    free(template_res.value.data);
    free(glyph_blob);
    if (!spliced) {
        return YETTY_ERR(yetty_yterm_shader_glyph_figure_ptr, "shader-glyph figure: splice failed");
    }

    /* Allocate as a yclass object so the figure carries a class header
     * (enables yclass dispatch). Typed body lives at obj + 1; the
     * embedded `base` is its first member. */
    struct yetty_yclass_ptr_result glyph_class_r = yetty_yterm_shader_glyph_class_get();
    if (YETTY_IS_ERR(glyph_class_r)) {
        free(spliced);
        return YETTY_ERR(yetty_yterm_shader_glyph_figure_ptr, "shader-glyph figure: class",
                         glyph_class_r);
    }
    struct yetty_yclass_object_ptr_result glyph_obj_r =
        yetty_yclass_object_alloc(glyph_class_r.value);
    if (YETTY_IS_ERR(glyph_obj_r)) {
        free(spliced);
        return YETTY_ERR(yetty_yterm_shader_glyph_figure_ptr, "shader-glyph figure: object_alloc",
                         glyph_obj_r);
    }
    struct yetty_yterm_shader_glyph_figure *f =
        (struct yetty_yterm_shader_glyph_figure *)(glyph_obj_r.value + 1);

    f->base.ops = figure_ops();
    f->base.self_obj = glyph_obj_r.value;
    f->base.rect = rect;
    /* Start dirty so the first frame uploads the buffer pointer + uniforms. */
    f->base.dirty = 1;

    f->text_layer = text_layer;
    f->context = context;
    f->request_render_fn = request_render_fn;
    f->request_render_userdata = request_render_userdata;
    f->grid_size = (struct yetty_ycore_grid_size){cols, rows};
    f->cell_size = (struct yetty_ycore_pixel_size){cell_width, cell_height};
    f->shader_source = spliced;
    f->shader_source_size = spliced_size;

    clock_gettime(CLOCK_MONOTONIC, &f->t0);

    /* Resource set */
    strncpy(f->rs.namespace, "shader_glyph", YETTY_YRENDER_NAME_MAX - 1);

    /* Two read-only storage buffers:
     *   buffers[0] "cells"      : full cell buffer (same as text-layer's).
     *   buffers[1] "instances"  : per-frame packed list of (cell_index,
     *                              local_id) for every shader-glyph cell. */
    f->rs.buffer_count = 2;
    strncpy(f->rs.buffers[0].name, "cells", YETTY_YRENDER_NAME_MAX - 1);
    strncpy(f->rs.buffers[0].wgsl_type, "array<u32>", YETTY_YRENDER_WGSL_TYPE_MAX - 1);
    f->rs.buffers[0].readonly = 1;
    strncpy(f->rs.buffers[1].name, "instances", YETTY_YRENDER_NAME_MAX - 1);
    strncpy(f->rs.buffers[1].wgsl_type, "array<u32>", YETTY_YRENDER_WGSL_TYPE_MAX - 1);
    f->rs.buffers[1].readonly = 1;

    init_uniforms(&f->rs);
    set_grid_size(&f->rs, (float)cols, (float)rows);
    set_cell_size(&f->rs, cell_width, cell_height);

    f->rs.pixel_size.width = (float)cols * cell_width;
    f->rs.pixel_size.height = (float)rows * cell_height;

    yetty_yrender_shader_code_set(&f->rs.shader, f->shader_source, f->shader_source_size);

    /* Seed buffers[0] with the text-layer's current cell pointer.
     * Refreshed each render in figure_pack_instances. */
    const uint8_t *cells_data = NULL;
    size_t cells_size = 0;
    yetty_yterm_terminal_layer_terminal_text_layer_get_cells(text_layer, &cells_data, &cells_size);
    f->rs.buffers[0].data = (uint8_t *)cells_data;
    f->rs.buffers[0].size = cells_size;
    f->rs.buffers[0].dirty = 1;

    /* Animation timer */
    f->event_loop = context->event_loop;
    f->listener.handler = on_anim_tick;
    if (f->event_loop && f->event_loop->ops && f->event_loop->ops->create_timer) {
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

        struct yetty_yevent_timer_id_result tres = f->event_loop->ops->create_timer(f->event_loop);
        if (YETTY_IS_OK(tres)) {
            f->timer_id = tres.value;
            f->event_loop->ops->config_timer(f->event_loop, f->timer_id, period_ms);
            f->event_loop->ops->register_timer_listener(f->event_loop, f->timer_id, &f->listener);
            f->timer_created = 1;
            yinfo("shader-glyph figure: anim timer fps=%d period=%dms", target_fps, period_ms);
        } else {
            ywarn("shader-glyph figure: create_timer failed; animations static");
        }
    }

    ydebug("shader-glyph figure: created %ux%u grid, %.1fx%.1f cells", cols, rows, cell_width,
           cell_height);

    return YETTY_OK(yetty_yterm_shader_glyph_figure_ptr, f);
}

struct yetty_yfigure_figure *yetty_yterm_shader_glyph_figure_as_figure(
    struct yetty_yterm_shader_glyph_figure *figure)
{
    return figure ? &figure->base : NULL;
}

struct yetty_ycore_void_result yetty_yterm_shader_glyph_figure_resize(
    struct yetty_yterm_shader_glyph_figure *f, struct yetty_ycore_grid_size grid_size,
    struct yetty_ycore_pixel_size cell_size)
{
    if (!f) {
        return YETTY_ERR(yetty_ycore_void, "shader-glyph figure resize: NULL figure");
    }
    if (cell_size.width <= 0.0f || cell_size.height <= 0.0f) {
        return YETTY_ERR(yetty_ycore_void, "shader-glyph figure resize: invalid cell size");
    }
    f->grid_size = grid_size;
    f->cell_size = cell_size;
    set_grid_size(&f->rs, (float)grid_size.cols, (float)grid_size.rows);
    set_cell_size(&f->rs, cell_size.width, cell_size.height);
    f->rs.pixel_size.width = (float)grid_size.cols * cell_size.width;
    f->rs.pixel_size.height = (float)grid_size.rows * cell_size.height;
    f->base.rect = (struct yetty_ycore_rectangle){
        .min = {.x = 0.0f, .y = 0.0f},
        .max = {.x = f->rs.pixel_size.width, .y = f->rs.pixel_size.height},
    };
    f->base.dirty = 1;
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yterm_shader_glyph_figure_set_visual_zoom(
    struct yetty_yterm_shader_glyph_figure *f, float scale, float offset_x, float offset_y)
{
    if (!f) {
        return YETTY_ERR(yetty_ycore_void, "shader-glyph figure set_visual_zoom: NULL figure");
    }
    set_visual_zoom(&f->rs, scale, offset_x, offset_y);
    f->base.dirty = 1;
    return YETTY_OK_VOID();
}

/* yclass class accessor (yetty_yterm_shader_glyph_class_get) + registration. */
#include "shader-glyph-figure.gen.c"
