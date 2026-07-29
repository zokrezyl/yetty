/*
 * grid-shader-glyph-layer.c — animated procedural "shader glyphs" for yvterm
 * (paints grid.c cells; the grid- prefix marks that pairing).
 *
 * Codepoints in the Supplementary PUA-B window (U+100000..U+100FFF) render as
 * per-cell animated fragment shaders instead of font glyphs. The shader bodies
 * live in <paths/shaders>/glyph-shaders/0xNN-name.wgsl; this layer assembles
 * them into one `render_shader_glyph(local_id, …)` dispatcher at create time,
 * splices it into an instanced quad shader, and each frame:
 *
 *   1. scans the grid's on-screen cells (resolving the ring via root_row) for
 *      shader-glyph codepoints,
 *   2. packs one instance per such cell — (column, row, local_id, fg, bg),
 *   3. draws an animated quad over each, on top of the text, using the same
 *      visual-zoom transform the text shader uses so they zoom and scroll in
 *      lockstep.
 *
 * An event-loop timer repaints while shader glyphs are visible and self-stops
 * when none are, so idle terminals quiesce. Plain-C render helper owned by the
 * yvterm:vterm figure (mirrors grid-sdf-layer); not a yclass class.
 */
#include "grid-shader-glyph-layer.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <webgpu/webgpu.h>

#include <yetty/yconfig/config.h>
#include <yetty/api/ytermsink/sink.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/util.h>
#include <yetty/yetty/yetty.h>
#include <yetty/yevent/event-loop.h>
#include <yetty/yevent/event.h>
#include "yetty/gen/impl/yfigure/figure.h"
#include <yetty/yfont/shader-glyph.h>
#include <yetty/yframework/yframework.h>
#include <yetty/yplatform/fs.h>
#include <yetty/yrender/gpu-resource-binder.h>
#include <yetty/yrender/gpu-resource-set.h>
#include <yetty/yrender/render-target.h>
#include <yetty/ytrace/ytrace.h>
#include "yetty/gen/impl/yvterm/grid.h"
#include <yetty/yvterm/shader-glyph-pua.h>
#include <yetty/webgpu/error.h>

/* Uniform slots (namespace "shader_glyph" → WGSL uniforms.shader_glyph_*). */
enum {
    U_GRID_SIZE = 0, /* vec2 cols,rows                */
    U_CELL_SIZE,     /* vec2 cell w,h                 */
    U_TIME,          /* f32 seconds since create      */
    U_VZ_SCALE,      /* f32 visual-zoom scale         */
    U_VZ_OFF,        /* vec2 visual-zoom source pan   */
    U_COUNT,
};

/* u32 words per packed instance: col, row, local_id, fg, bg. */
enum { SG_INSTANCE_WORDS = 5 };

struct yetty_yvterm_shader_glyph_layer {
    const struct yetty_context *context;
    /* The owning yvterm:vterm figure object — the anim timer marks it dirty so
     * the next frame repaints. Borrowed. */
    struct yetty_yclass_object *owner_figure;
    /* Terminal-host sink — request_render is dispatched on it. Borrowed. */
    struct yetty_yclass_object *sink;

    int headless; /* no GPU / no shader → render is a no-op */

    /* Assembled shader source (template with the glyph dispatcher spliced in). */
    char *shader_source;
    size_t shader_source_size;

    /* GPU plumbing: the binder owns the pipeline + bind groups, compiled lazily
     * on first submit/finalize. */
    struct yetty_yrender_gpu_resource_set rs;
    struct yetty_yrender_gpu_resource_binder *binder;
    int binder_finalized;

    /* Per-frame packed instance list (SG_INSTANCE_WORDS u32 per instance). */
    uint32_t *instances;
    size_t instance_word_cap;
    uint32_t instance_count;

    /* Animation co-driver. Each rendered frame also self-schedules the next via
     * request_render (the reliable driver — the timer alone does not pump while
     * the loop idles on the window source), but a periodic timer additionally
     * keeps the loop iterating so the animation runs smoothly rather than at the
     * loop's slow idle cadence. Mirrors the yshadertoy figure. */
    struct yetty_yevent_event_loop *event_loop;
    yetty_yevent_timer_id timer_id;
    int timer_created;
    int timer_running;
    int active; /* last render saw at least one shader-glyph cell */
    struct yetty_yevent_event_listener listener;
};

/*===========================================================================
 * Uniform helpers
 *=========================================================================*/

static void sg_init_uniforms(struct yetty_yrender_gpu_resource_set *rs)
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
    rs->uniforms[U_TIME].f32 = 0.0f;
    rs->uniforms[U_VZ_SCALE].f32 = 1.0f;
    rs->uniforms[U_VZ_OFF].vec2[0] = 0.0f;
    rs->uniforms[U_VZ_OFF].vec2[1] = 0.0f;
}

/*===========================================================================
 * Glyph-shader assembly
 *
 * Scan <glyph_dir>/0xNN-name.wgsl, read each allow-listed file, sort by
 * local_id, concatenate the bodies, then emit a `render_shader_glyph(...)`
 * switch dispatcher. Files starting with '_' are prelude libraries
 * (e.g. _util.wgsl) concatenated first so glyph bodies can call their helpers.
 * The result is spliced into the template at SHADER_GLYPHS_PLACEHOLDER.
 *=========================================================================*/

struct sg_glyph_entry {
    uint32_t local_id;
    char *body;
    size_t body_size;
};

static int sg_glyph_entry_cmp(const void *a, const void *b)
{
    uint32_t la = ((const struct sg_glyph_entry *)a)->local_id;
    uint32_t lb = ((const struct sg_glyph_entry *)b)->local_id;
    return (la > lb) - (la < lb);
}

/* Case-insensitive match of a glyph filename stem against an allow-list entry
 * (e.g. "0x0001-pulse.wgsl" vs "0x0001-pulse"). */
static int sg_glyph_name_matches(const char *filename, const char *want)
{
    size_t flen = strlen(filename);
    if (flen <= 5 || strcmp(filename + flen - 5, ".wgsl") != 0) {
        return 0;
    }
    size_t stem_len = flen - 5;
    if (strlen(want) != stem_len) {
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

/* Strict allow-list — missing key, empty list, or NULL config loads nothing.
 * The user explicitly enumerates which glyphs to compile in (shaders/preload/
 * glyphs), avoiding dragging heavy shaders into the merged WGSL. */
static int sg_glyph_allowed(struct yetty_yconfig_config *config, const char *filename)
{
    if (!config) {
        return 0;
    }
    int count = config->ops->get_array_count(config, "shaders/preload/glyphs");
    for (int i = 0; i < count; i++) {
        const char *want = config->ops->get_array_item(config, "shaders/preload/glyphs", i, NULL);
        if (want && sg_glyph_name_matches(filename, want)) {
            return 1;
        }
    }
    return 0;
}

static struct yetty_ycore_buffer_result sg_assemble_glyph_shaders(
    const char *glyph_dir, struct yetty_yconfig_config *config)
{
    struct yetty_yplatform_dir *dir = yetty_yplatform_dir_open(glyph_dir);
    if (!dir) {
        ywarn("shader-glyph: open(%s) failed", glyph_dir);
        return YETTY_ERR(yetty_ycore_buffer, "shader-glyph: dir open failed");
    }
    int allow_list_size =
        config ? config->ops->get_array_count(config, "shaders/preload/glyphs") : 0;

    struct sg_glyph_entry *entries = NULL;
    size_t cap = 0, count = 0;

    struct yetty_ycore_buffer prelude_bufs[8];
    char prelude_names[8][128];
    size_t prelude_count = 0;

    struct yetty_yplatform_dir_entry de;
    while (yetty_yplatform_dir_next(dir, &de)) {
        const char *name = de.name;
        size_t len = strlen(name);
        if (len < 6 || strcmp(name + len - 5, ".wgsl") != 0) {
            continue;
        }

        char path[768];
        snprintf(path, sizeof(path), "%s/%s", glyph_dir, name);

        if (name[0] == '_') {
            if (allow_list_size <= 0 || prelude_count >= 8) {
                continue;
            }
            struct yetty_ycore_buffer_result br = yetty_ycore_read_file(path);
            if (YETTY_IS_ERR(br)) {
                yetty_ycore_error_destroy(br.error);
                continue;
            }
            prelude_bufs[prelude_count] = br.value;
            strncpy(prelude_names[prelude_count], name, sizeof(prelude_names[0]) - 1);
            prelude_names[prelude_count][sizeof(prelude_names[0]) - 1] = 0;
            prelude_count++;
            continue;
        }

        if (strncmp(name, "0x", 2) != 0 || !sg_glyph_allowed(config, name)) {
            continue;
        }

        uint32_t local_id = (uint32_t)strtoul(name + 2, NULL, 16);
        struct yetty_ycore_buffer_result br = yetty_ycore_read_file(path);
        if (YETTY_IS_ERR(br)) {
            ywarn("shader-glyph: read %s: %s", path, br.error.msg);
            yetty_ycore_error_destroy(br.error);
            continue;
        }

        if (count >= cap) {
            size_t new_cap = cap ? cap * 2 : 32;
            struct sg_glyph_entry *grown = realloc(entries, new_cap * sizeof(*entries));
            if (!grown) {
                free(br.value.data);
                break;
            }
            entries = grown;
            cap = new_cap;
        }
        entries[count].local_id = local_id;
        entries[count].body = (char *)br.value.data;
        entries[count].body_size = br.value.size;
        count++;
    }
    yetty_yplatform_dir_close(dir);

    qsort(entries, count, sizeof(struct sg_glyph_entry), sg_glyph_entry_cmp);

    size_t total = 256 + count * 128;
    for (size_t i = 0; i < prelude_count; i++) {
        total += prelude_bufs[i].size + 1;
    }
    for (size_t i = 0; i < count; i++) {
        total += entries[i].body_size + 1;
    }
    char *out = malloc(total);
    if (!out) {
        for (size_t i = 0; i < prelude_count; i++) {
            free(prelude_bufs[i].data);
        }
        for (size_t i = 0; i < count; i++) {
            free(entries[i].body);
        }
        free(entries);
        return YETTY_ERR(yetty_ycore_buffer, "shader-glyph: assemble alloc failed");
    }

    size_t off = 0;
    for (size_t i = 0; i < prelude_count; i++) {
        memcpy(out + off, prelude_bufs[i].data, prelude_bufs[i].size);
        off += prelude_bufs[i].size;
        out[off++] = '\n';
        free(prelude_bufs[i].data);
    }
    for (size_t i = 0; i < count; i++) {
        memcpy(out + off, entries[i].body, entries[i].body_size);
        off += entries[i].body_size;
        out[off++] = '\n';
    }

    int written = snprintf(out + off, total - off,
                           "fn render_shader_glyph(local_id: u32, uv: vec2<f32>, time: f32,\n"
                           "                       fg: vec3<f32>, bg: vec3<f32>,\n"
                           "                       pixel_pos: vec2<f32>) -> vec3<f32> {\n"
                           "    switch (local_id) {\n");
    if (written > 0) {
        off += (size_t)written;
    }
    for (size_t i = 0; i < count; i++) {
        written = snprintf(out + off, total - off,
                           "        case %uu: { return shader_glyph_%u(uv, time, fg, bg, "
                           "pixel_pos); }\n",
                           entries[i].local_id, entries[i].local_id);
        if (written > 0) {
            off += (size_t)written;
        }
    }
    written = snprintf(out + off, total - off,
                       "        default: { return mix(bg, fg, 0.5); }\n"
                       "    }\n"
                       "}\n");
    if (written > 0) {
        off += (size_t)written;
    }

    for (size_t i = 0; i < count; i++) {
        free(entries[i].body);
    }
    free(entries);

    ydebug("shader-glyph: assembled %zu prelude + %zu glyphs, %zu bytes WGSL", prelude_count, count,
           off);
    struct yetty_ycore_buffer outbuf = {.data = (uint8_t *)out, .size = off, .capacity = total};
    return YETTY_OK(yetty_ycore_buffer, outbuf);
}

/* Substitute the first occurrence of `marker` in `template` with `replacement`.
 * Returns a malloc'd NUL-terminated buffer; caller frees. */
static char *sg_splice_marker(const char *template, size_t template_size, const char *marker,
                              const char *replacement, size_t replacement_size, size_t *out_size)
{
    size_t marker_len = strlen(marker);
    const char *found = NULL;
    for (size_t i = 0; i + marker_len <= template_size; i++) {
        if (memcmp(template + i, marker, marker_len) == 0) {
            found = template + i;
            break;
        }
    }
    size_t before, after_off, after;
    if (found) {
        before = (size_t)(found - template);
        after_off = before + marker_len;
        after = template_size - after_off;
    } else {
        ywarn("shader-glyph: splice marker '%s' not found, appending", marker);
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

/* The instanced quad shader. The binder injects its bindings (the flat
 * storage_buffer + the uniforms struct + the per-buffer offset constants) at
 * RENDER_LAYER_BINDINGS_PLACEHOLDER; the assembled glyph dispatcher is spliced
 * at SHADER_GLYPHS_PLACEHOLDER. The vertex transform mirrors the text shader's
 * visual-zoom math exactly so glyphs stay welded to their cells under zoom. */
static const char *sg_template_wgsl(void)
{
    static const char src[] =
        "// RENDER_LAYER_BINDINGS_PLACEHOLDER\n"
        "struct VertexInput  { @location(0) position: vec2<f32>, };\n"
        "struct VertexOutput {\n"
        "    @builtin(position) position: vec4<f32>,\n"
        "    @location(0) @interpolate(linear) local_uv: vec2<f32>,\n"
        "    @location(1) @interpolate(flat) local_id: u32,\n"
        "    @location(2) @interpolate(linear) pixel_pos: vec2<f32>,\n"
        "    @location(3) @interpolate(flat) fg: vec3<f32>,\n"
        "    @location(4) @interpolate(flat) bg: vec3<f32>,\n"
        "};\n"
        "fn sg_unpack_rgb(p: u32) -> vec3<f32> {\n"
        "    return vec3<f32>(f32(p & 0xFFu) / 255.0, f32((p >> 8u) & 0xFFu) / 255.0,\n"
        "                     f32((p >> 16u) & 0xFFu) / 255.0);\n"
        "}\n"
        "@vertex\n"
        "fn vs_main(input: VertexInput, @builtin(instance_index) inst: u32) -> VertexOutput {\n"
        "    var output: VertexOutput;\n"
        "    let base = uniforms.shader_glyph_instances_offset + inst * 5u;\n"
        "    let col = f32(storage_buffer[base + 0u]);\n"
        "    let row = f32(storage_buffer[base + 1u]);\n"
        "    output.local_id = storage_buffer[base + 2u];\n"
        "    output.fg = sg_unpack_rgb(storage_buffer[base + 3u]);\n"
        "    output.bg = sg_unpack_rgb(storage_buffer[base + 4u]);\n"
        "    let grid_size = uniforms.shader_glyph_grid_size;\n"
        "    let cell_size = uniforms.shader_glyph_cell_size;\n"
        "    let grid_pixel_w = grid_size.x * cell_size.x;\n"
        "    let grid_pixel_h = grid_size.y * cell_size.y;\n"
        "    let corner_uv = input.position * 0.5 + 0.5;\n"
        "    let px = (col + corner_uv.x) * cell_size.x;\n"
        "    let py = (row + corner_uv.y) * cell_size.y;\n"
        "    let vz_scale = uniforms.shader_glyph_visual_zoom_scale;\n"
        "    let vz_off   = uniforms.shader_glyph_visual_zoom_off;\n"
        "    let vz_center = vec2<f32>(grid_pixel_w * 0.5, grid_pixel_h * 0.5);\n"
        "    let source_px = vec2<f32>(px, py);\n"
        "    let screen_px = (source_px - vz_center - vz_off) * vz_scale + vz_center;\n"
        "    let ndc_x = screen_px.x / grid_pixel_w * 2.0 - 1.0;\n"
        "    let ndc_y = 1.0 - screen_px.y / grid_pixel_h * 2.0;\n"
        "    output.position = vec4<f32>(ndc_x, ndc_y, 0.0, 1.0);\n"
        "    output.local_uv = corner_uv;\n"
        "    output.pixel_pos = source_px;\n"
        "    return output;\n"
        "}\n"
        "// SHADER_GLYPHS_PLACEHOLDER\n"
        "@fragment\n"
        "fn fs_main(input: VertexOutput) -> @location(0) vec4<f32> {\n"
        "    let t = uniforms.shader_glyph_time;\n"
        "    let color = render_shader_glyph(input.local_id, input.local_uv, t, input.fg,\n"
        "                                    input.bg, input.pixel_pos);\n"
        "    return vec4<f32>(color, 1.0);\n"
        "}\n";
    return src;
}

/*===========================================================================
 * Animation timer (co-driver — see the struct comment)
 *=========================================================================*/

static inline struct yetty_yvterm_shader_glyph_layer *sg_layer_from_listener(
    struct yetty_yevent_event_listener *listener)
{
    return (
        struct yetty_yvterm_shader_glyph_layer *)((char *)listener -
                                                  offsetof(struct yetty_yvterm_shader_glyph_layer,
                                                           listener));
}

static void sg_timer_stop(struct yetty_yvterm_shader_glyph_layer *layer)
{
    if (!layer->timer_created || !layer->timer_running) {
        return;
    }
    layer->event_loop->ops->stop_timer(layer->event_loop, layer->timer_id);
    layer->timer_running = 0;
}

static void sg_timer_start(struct yetty_yvterm_shader_glyph_layer *layer)
{
    if (!layer->timer_created || layer->timer_running) {
        return;
    }
    layer->event_loop->ops->start_timer(layer->event_loop, layer->timer_id);
    layer->timer_running = 1;
}

/* Timer tick: keep the loop iterating while glyphs are on screen by marking the
 * owning figure dirty and requesting a render. Self-stops once the last
 * shader-glyph cell is gone (active is cleared by a render that finds none). */
static struct yetty_ycore_int_result sg_on_anim_tick(struct yetty_yevent_event_listener *listener,
                                                     const struct yetty_yui_event *event)
{
    (void)event;
    struct yetty_yvterm_shader_glyph_layer *layer = sg_layer_from_listener(listener);
    if (!layer->active) {
        sg_timer_stop(layer);
        return YETTY_OK(yetty_ycore_int, 0);
    }
    if (layer->owner_figure) {
        struct yetty_ycore_void_result set_r =
            yetty_yfigure_figure_dirty_set(layer->owner_figure, 1);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, set_r, "shader-glyph tick: dirty_set");
    }
    if (layer->sink) {
        struct yetty_ycore_void_result r = yetty_ytermsink_request_render(layer->sink);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, r, "shader-glyph tick: request_render");
    }
    return YETTY_OK(yetty_ycore_int, 0);
}

/*===========================================================================
 * Cell scan → instance packing
 *=========================================================================*/

/* Walk the on-screen cells (through the resolved view-window slots the text
 * pass draws — ring or archive cache) and pack one instance per shader-glyph
 * cell. Returns the count; grows layer->instances as needed. */
static uint32_t sg_pack_instances(struct yetty_yvterm_shader_glyph_layer *layer,
                                  struct yetty_yclass_object *grid_obj, uint32_t cols,
                                  uint32_t rows, const uint32_t *window_slots, uint32_t window_rows)
{
    uint32_t count = 0;
    for (uint32_t r = 0; r < rows && r < window_rows; ++r) {
        uint32_t slot = window_slots[r];
        struct yetty_yvterm_text_cell_const_ptr_result cells_res =
            yetty_yvterm_grid_slot_cells(grid_obj, slot);
        if (YETTY_IS_ERR(cells_res)) {
            yetty_ycore_error_destroy(cells_res.error);
            continue;
        }
        const struct yetty_yvterm_text_cell *cells = cells_res.value;
        if (!cells) {
            continue;
        }
        for (uint32_t c = 0; c < cols; ++c) {
            uint32_t cp = cells[c].codepoint;
            if (!yetty_shader_glyph_codepoint_in_range(cp) ||
                !yetty_yfont_shader_glyph_codepoint_exists(cp)) {
                continue;
            }
            size_t need_words = (size_t)(count + 1) * SG_INSTANCE_WORDS;
            if (need_words > layer->instance_word_cap) {
                size_t new_cap = layer->instance_word_cap ? layer->instance_word_cap * 2
                                                          : (size_t)SG_INSTANCE_WORDS * 64;
                while (new_cap < need_words) {
                    new_cap *= 2;
                }
                uint32_t *grown = realloc(layer->instances, new_cap * sizeof(uint32_t));
                if (!grown) {
                    return count; /* keep what we have */
                }
                layer->instances = grown;
                layer->instance_word_cap = new_cap;
            }
            uint32_t *inst = &layer->instances[(size_t)count * SG_INSTANCE_WORDS];
            inst[0] = c;
            inst[1] = r;
            inst[2] = cp - YETTY_SHADER_GLYPH_PUA_BASE;
            inst[3] = cells[c].fg;
            inst[4] = cells[c].bg;
            count++;
        }
    }
    layer->instance_count = count;
    return count;
}

/*===========================================================================
 * Public API
 *=========================================================================*/

struct yetty_yvterm_shader_glyph_layer_ptr_result yetty_yvterm_shader_glyph_layer_create(
    const struct yetty_context *context, struct yetty_yclass_object *owner_figure,
    struct yetty_yclass_object *sink)
{
    struct yetty_yvterm_shader_glyph_layer *layer = calloc(1, sizeof(*layer));
    if (!layer) {
        return YETTY_ERR(yetty_yvterm_shader_glyph_layer_ptr, "shader-glyph: alloc oom");
    }
    layer->context = context;
    layer->owner_figure = owner_figure;
    layer->sink = sink;

    if (!context || !context->runtime || !context->runtime->gpu.device) {
        layer->headless = 1;
        return YETTY_OK(yetty_yvterm_shader_glyph_layer_ptr, layer);
    }
    struct yetty_yframework *runtime = context->runtime;
    struct yetty_yconfig_config *config = runtime->config;
    const char *shaders_dir = config ? config->ops->get_string(config, "paths/shaders", "") : "";
    char glyph_dir[640];
    snprintf(glyph_dir, sizeof(glyph_dir), "%s/glyph-shaders", shaders_dir ? shaders_dir : "");

    /* Assemble the glyph dispatcher and splice it into the template. A failure
     * here disables the feature (headless) but never fails terminal creation. */
    struct yetty_ycore_buffer_result glyph_res = sg_assemble_glyph_shaders(glyph_dir, config);
    if (YETTY_IS_ERR(glyph_res)) {
        ywarn("shader-glyph: assemble failed (%s) — feature disabled", glyph_res.error.msg);
        yetty_ycore_error_destroy(glyph_res.error);
        layer->headless = 1;
        return YETTY_OK(yetty_yvterm_shader_glyph_layer_ptr, layer);
    }
    const char *template = sg_template_wgsl();
    size_t spliced_size = 0;
    char *spliced =
        sg_splice_marker(template, strlen(template), "// SHADER_GLYPHS_PLACEHOLDER",
                         (const char *)glyph_res.value.data, glyph_res.value.size, &spliced_size);
    free(glyph_res.value.data);
    if (!spliced) {
        layer->headless = 1;
        return YETTY_OK(yetty_yvterm_shader_glyph_layer_ptr, layer);
    }
    layer->shader_source = spliced;
    layer->shader_source_size = spliced_size;

    /* Resource set: one read-only storage buffer of packed instances. */
    strncpy(layer->rs.namespace, "shader_glyph", YETTY_YRENDER_NAME_MAX - 1);
    layer->rs.buffer_count = 1;
    strncpy(layer->rs.buffers[0].name, "instances", YETTY_YRENDER_NAME_MAX - 1);
    strncpy(layer->rs.buffers[0].wgsl_type, "array<u32>", YETTY_YRENDER_WGSL_TYPE_MAX - 1);
    layer->rs.buffers[0].readonly = 1;
    sg_init_uniforms(&layer->rs);
    yetty_yrender_shader_code_set(&layer->rs.shader, layer->shader_source,
                                  layer->shader_source_size);

    struct yetty_yrender_gpu_resource_binder_result br = yetty_yrender_gpu_resource_binder_create(
        runtime->gpu.device, runtime->gpu.queue, runtime->gpu.surface_format,
        runtime->gpu.allocator);
    if (YETTY_IS_ERR(br)) {
        ywarn("shader-glyph: binder create failed (%s) — feature disabled", br.error.msg);
        yetty_ycore_error_destroy(br.error);
        free(layer->shader_source);
        layer->shader_source = NULL;
        layer->headless = 1;
        return YETTY_OK(yetty_yvterm_shader_glyph_layer_ptr, layer);
    }
    layer->binder = br.value;

    /* Animation co-driver timer (armed on the first frame that has glyphs). */
    layer->event_loop = context->event_loop;
    layer->listener.handler = sg_on_anim_tick;
    if (layer->event_loop && layer->event_loop->ops && layer->event_loop->ops->create_timer) {
        int target_fps =
            config ? config->ops->get_int(config, "terminal/shader-glyph-layer/target-fps", 60)
                   : 60;
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
        struct yetty_yevent_timer_id_result tr =
            layer->event_loop->ops->create_timer(layer->event_loop);
        if (YETTY_IS_OK(tr)) {
            layer->timer_id = tr.value;
            layer->event_loop->ops->config_timer(layer->event_loop, layer->timer_id, period_ms);
            layer->event_loop->ops->register_timer_listener(layer->event_loop, layer->timer_id,
                                                            &layer->listener);
            layer->timer_created = 1;
            yinfo("shader-glyph: anim timer fps=%d period=%dms", target_fps, period_ms);
        } else {
            yetty_ycore_error_destroy(tr.error);
            ywarn("shader-glyph: create_timer failed; animation runs at idle cadence");
        }
    }

    ydebug("shader-glyph: layer ready (%s)", glyph_dir);
    return YETTY_OK(yetty_yvterm_shader_glyph_layer_ptr, layer);
}

void yetty_yvterm_shader_glyph_layer_destroy(struct yetty_yvterm_shader_glyph_layer *layer)
{
    if (!layer) {
        return;
    }
    if (layer->timer_created && layer->event_loop && layer->event_loop->ops) {
        if (layer->timer_running && layer->event_loop->ops->stop_timer) {
            layer->event_loop->ops->stop_timer(layer->event_loop, layer->timer_id);
        }
        if (layer->event_loop->ops->deregister_timer_listener) {
            layer->event_loop->ops->deregister_timer_listener(layer->event_loop, layer->timer_id,
                                                              &layer->listener);
        }
        if (layer->event_loop->ops->destroy_timer) {
            layer->event_loop->ops->destroy_timer(layer->event_loop, layer->timer_id);
        }
    }
    if (layer->binder) {
        layer->binder->ops->destroy(layer->binder);
    }
    free(layer->shader_source);
    free(layer->instances);
    free(layer);
}

struct yetty_ycore_void_result yetty_yvterm_shader_glyph_layer_render(
    struct yetty_yvterm_shader_glyph_layer *layer, struct yetty_yclass_object *grid_obj,
    struct yetty_ydraw_target *target, struct yetty_ycore_rectangle rect, float cell_width,
    float cell_height, uint32_t cols, uint32_t rows, const uint32_t *window_slots,
    uint32_t window_rows, float visual_zoom_scale, float visual_zoom_off_x, float visual_zoom_off_y)
{
    if (!layer || layer->headless || !layer->binder || !grid_obj || !target || !target->ops ||
        !target->ops->get_view) {
        return YETTY_OK_VOID();
    }
    if (cols == 0 || rows == 0 || cell_width <= 0.0f || cell_height <= 0.0f || !window_slots) {
        return YETTY_OK_VOID();
    }

    uint32_t count = sg_pack_instances(layer, grid_obj, cols, rows, window_slots, window_rows);
    if (count == 0) {
        /* No shader glyphs on screen — stop the co-driver timer and don't
         * schedule another frame; a later content change re-enters render and
         * restarts the loop. */
        layer->active = 0;
        sg_timer_stop(layer);
        return YETTY_OK_VOID();
    }
    layer->active = 1;
    sg_timer_start(layer);

    layer->rs.uniforms[U_GRID_SIZE].vec2[0] = (float)cols;
    layer->rs.uniforms[U_GRID_SIZE].vec2[1] = (float)rows;
    layer->rs.uniforms[U_CELL_SIZE].vec2[0] = cell_width;
    layer->rs.uniforms[U_CELL_SIZE].vec2[1] = cell_height;
    /* Shared frame clock: identical value across every shader this frame. */
    layer->rs.uniforms[U_TIME].f32 = (float)layer->context->runtime->frame_time_sec;
    layer->rs.uniforms[U_VZ_SCALE].f32 = visual_zoom_scale > 0.0f ? visual_zoom_scale : 1.0f;
    layer->rs.uniforms[U_VZ_OFF].vec2[0] = visual_zoom_off_x;
    layer->rs.uniforms[U_VZ_OFF].vec2[1] = visual_zoom_off_y;

    layer->rs.buffers[0].data = (uint8_t *)layer->instances;
    layer->rs.buffers[0].size = (size_t)count * SG_INSTANCE_WORDS * sizeof(uint32_t);
    layer->rs.buffers[0].dirty = 1;
    layer->rs.instance_count = count;

    struct yetty_ycore_void_result sr = layer->binder->ops->submit(layer->binder, &layer->rs);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "shader-glyph render: binder submit");
    if (!layer->binder_finalized) {
        struct yetty_ycore_void_result fr = layer->binder->ops->finalize(layer->binder);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, fr, "shader-glyph render: binder finalize");
        layer->binder_finalized = 1;
    }
    struct yetty_ycore_void_result ur = layer->binder->ops->update(layer->binder);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, ur, "shader-glyph render: binder update");

    WGPUTextureView view = target->ops->get_view(target);
    if (!view) {
        return YETTY_OK_VOID();
    }
    WGPURenderPipeline pipeline = layer->binder->ops->get_pipeline(layer->binder);
    WGPUBuffer quad_vb = layer->binder->ops->get_quad_vertex_buffer(layer->binder);
    if (!pipeline || !quad_vb) {
        return YETTY_OK_VOID();
    }

    struct yetty_yframework_gpu_context *gpu = &layer->context->runtime->gpu;
    /* Pane-local rect → framebuffer viewport (same offset math as the text
     * pass), clamped to the target viewport. */
    float vx = target->viewport.x + rect.min.x;
    float vy = target->viewport.y + rect.min.y;
    float w = rect.max.x - rect.min.x;
    float h = rect.max.y - rect.min.y;
    if (w <= 0.0f || h <= 0.0f) {
        return YETTY_OK_VOID();
    }

    WGPUCommandEncoderDescriptor enc_desc = {0};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(gpu->device, &enc_desc);
    if (!encoder) {
        return YETTY_ERR(yetty_ycore_void, "shader-glyph render: command encoder");
    }
    WGPURenderPassColorAttachment ca = {0};
    ca.view = view;
    ca.loadOp = WGPULoadOp_Load; /* paint on top of the text */
    ca.storeOp = WGPUStoreOp_Store;
    ca.clearValue = (WGPUColor){0.0, 0.0, 0.0, 0.0};
    ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    WGPURenderPassDescriptor pass_desc = {0};
    pass_desc.colorAttachmentCount = 1;
    pass_desc.colorAttachments = &ca;
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &pass_desc);
    if (!pass) {
        wgpuCommandEncoderRelease(encoder);
        return YETTY_ERR(yetty_ycore_void, "shader-glyph render: begin pass");
    }
    wgpuRenderPassEncoderSetViewport(pass, vx, vy, w, h, 0.0f, 1.0f);
    float tx0 = target->viewport.x, ty0 = target->viewport.y;
    float tx1 = tx0 + target->viewport.w, ty1 = ty0 + target->viewport.h;
    float sx0 = vx > tx0 ? vx : tx0;
    float sy0 = vy > ty0 ? vy : ty0;
    float sx1 = (vx + w) < tx1 ? (vx + w) : tx1;
    float sy1 = (vy + h) < ty1 ? (vy + h) : ty1;
    if (sx1 > sx0 && sy1 > sy0) {
        wgpuRenderPassEncoderSetScissorRect(pass, (uint32_t)sx0, (uint32_t)sy0,
                                            (uint32_t)(sx1 - sx0), (uint32_t)(sy1 - sy0));
        wgpuRenderPassEncoderSetPipeline(pass, pipeline);
        struct yetty_ycore_void_result bind_res = layer->binder->ops->bind(layer->binder, pass, 0);
        if (YETTY_IS_ERR(bind_res)) {
            yetty_ycore_error_destroy(bind_res.error);
        } else {
            wgpuRenderPassEncoderSetVertexBuffer(pass, 0, quad_vb, 0, WGPU_WHOLE_SIZE);
            wgpuRenderPassEncoderDraw(pass, 6, count, 0, 0);
            ydebug("shader-glyph: drew %u glyph(s) t=%.2f vp=(%.0f,%.0f,%.0f,%.0f)", count,
                   (double)layer->rs.uniforms[U_TIME].f32, vx, vy, w, h);
        }
    }
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    WGPUCommandBufferDescriptor cmd_desc = {0};
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, &cmd_desc);
    wgpuQueueSubmit(gpu->queue, 1, &cmd);
    wgpuCommandBufferRelease(cmd);
    wgpuCommandEncoderRelease(encoder);

    /* Animated glyphs are on screen — schedule the next frame. The uv timer
     * does not pump while the event loop idles on the window source, so (like
     * the yshadertoy / yvideo figures) each animated frame drives the next:
     * mark the owning figure dirty and wake the loop via request_render. This
     * stops on its own once a later frame finds no shader-glyph cells. */
    if (layer->owner_figure) {
        struct yetty_ycore_void_result dr = yetty_yfigure_figure_dirty_set(layer->owner_figure, 1);
        if (YETTY_IS_ERR(dr)) {
            yetty_ycore_error_destroy(dr.error);
        }
    }
    if (layer->sink) {
        struct yetty_ycore_void_result rr = yetty_ytermsink_request_render(layer->sink);
        if (YETTY_IS_ERR(rr)) {
            yetty_ycore_error_destroy(rr.error);
        }
    }
    return YETTY_OK_VOID();
}
