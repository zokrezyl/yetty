/* sdf-layer.c — yvterm's SDF / glyph / text render backend.
 *
 * Rasterises the raw ydraw drawable records stored per-line on yvterm's grid
 * ring (SDF shapes, GLYPH prims, TEXT_DRAWABLE_LIST runs, FONT resources). Each
 * record is anchored to its line through the shader's rolling-row mechanism:
 * the prim carries a (signed) visible-row number; the fragment shader shifts the
 * sampled coordinate by (rolling_row * cell_height), so a figure tracks the row
 * its text sits on, in both the live and scrolled-back views.
 *
 * The GPU plumbing — resource binder, the generated ysdf.gen.wgsl, the font
 * dispatcher, the combined shader — is the SHARED yrender/ydraw machinery (the
 * same building blocks ygrid uses). This file is a standalone renderer: it does
 * NOT instantiate ygrid; it sources primitives from yvterm's own grid.
 *
 * The model follows ygrid's bucket+stage+draw pipeline, simplified to yvterm's
 * needs: no entity tree, no id index, no composite handling (composites render
 * through vterm's own rich pass). Staging is rebuilt every render from the
 * visible ring — render is dirty-gated, so for static content it happens once
 * and on each scroll.
 */
#include "sdf-layer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <webgpu/webgpu.h>

#include <yetty/yclass/class.h>
#include <yetty/yconfig/config.h>
#include <yetty/ycore/util.h>
#include <yetty/ydraw-core/composite.h>
#include <yetty/ydraw-core/font-resource.h>
#include <yetty/ydraw-core/text-drawable-list.h>
#include <yetty/yetty/yetty.h>
#include <yetty/yfont/font.h>
#include <yetty/yfont/font-cache.h>
#include <yetty/yfont/raster-font.h>
#include <yetty/yframework/yframework.h>
#include <yetty/ymsdf/generator.h>
#include <yetty/yplatform/fs.h>
#include <yetty/yplatform/paths.h>
#include <yetty/yrender/font-dispatcher.h>
#include <yetty/yrender/gpu-resource-binder.h>
#include <yetty/yrender/gpu-resource-set.h>
#include <yetty/yrender/render-target.h>
#include <yetty/yrender/types.h>
#include <yetty/ysdf/handler.h>
#include <yetty/ytrace/ytrace.h>
#include <yetty/yvterm/grid.h>

#include "ligature-cells.h"

/* GLYPH primitive type — matches the shader's YDRAW_SDF_GLYPH (and ygrid). */
#define YVTERM_SDF_GLYPH_TYPE 200u

/* How many rows BELOW the viewport bottom a bottom-anchored block is scanned
 * for (capped by the live scroll distance). Bounds the tallest block kept alive
 * in a scrolled-back view; per-prim AABB clipping culls anything that doesn't
 * actually reach the pane. */
#define YVTERM_SDF_ANCHOR_LOOKAHEAD_ROWS 256u

/* Uniform slots — same names/order/types ygrid uses so the (copied) layer
 * shader's ydraw_ydraw_* fields resolve. */
#define U_GRID_SIZE 0
#define U_CELL_SIZE 1
#define U_ROLLING_ROW_0 2
#define U_PRIM_COUNT 3
#define U_VZ_SCALE 4
#define U_VZ_OFF 5
#define U_CZ_SCALE 6
#define U_CZ_OFF 7
#define U_VIEW_SIZE 8
#define U_COUNT 9

/* Producer-assigned font ids are small; cap the slot map at a generous size. */
#define YVTERM_SDF_MAX_FONT_IDS 256

/* One complex-script shaping face is loaded per script that appears on screen
 * (Arabic, Devanagari, Bengali, Tamil, Thai, ...); cap the set generously. */
#define YVTERM_SDF_MAX_SHAPING_FACES 16

/* A lazily-loaded raster shaping face for terminal complex-script runs. Owned
 * by the layer (created directly from a bundled TTF, not via the font cache). */
struct sdf_shaping_face {
    char file[64];                 /* bundled TTF filename — dedup key */
    struct yetty_yfont_font *font; /* NULL when the load failed (do not retry) */
    uint32_t slot;                 /* font slot this face occupies */
    int attempted;                 /* 1 once a load was tried (success or fail) */
};

/* Per-prim metadata recorded at index time (one render's worth). */
struct sdf_prim_meta {
    uint32_t payload_offset; /* byte offset into layer->bytes of the wire record */
    uint32_t payload_words;  /* record length in u32 words */
    uint32_t type;
    float min_x, min_y, max_x, max_y; /* AABB in line-local pixels */
    int32_t rolling_row;              /* signed visible row this prim anchors to */
};

/* Per-cell bucket: prim indices whose screen extent overlaps the cell. */
struct sdf_cell {
    uint32_t *indices;
    uint32_t count;
    uint32_t cap;
};

struct yetty_yvterm_sdf_layer {
    int headless;

    /* Borrowed GPU handles. */
    WGPUDevice device;
    WGPUQueue queue;
    WGPUTextureFormat target_format;
    struct yetty_ydraw_gpu_allocator *allocator;
    struct yetty_ymsdf_generator *msdf_generator;
    char shaders_dir[512];
    char cache_dir[512];
    char data_dir[512];

    /* Pipeline + resources. */
    struct yetty_yrender_gpu_resource_binder *binder;
    int binder_finalized;
    struct yetty_yrender_gpu_resource_set rs;
    struct yetty_yrender_gpu_resource_set sdf_lib_rs;
    struct yetty_ycore_buffer sdf_lib_code;
    struct yetty_ycore_buffer layer_shader_code;
    char *combined_shader;
    size_t combined_shader_size;

    /* Fonts (slot 0 = default, 1..N = wire-shipped). Persist across frames. */
    struct yetty_yfont_cache *font_cache;
    struct yetty_yfont_font *fonts[YETTY_YRENDER_RS_MAX_CHILDREN];
    uint32_t font_count;
    uint32_t font_generation;
    uint32_t last_emitted_font_generation;
    int32_t wire_font_slot[YVTERM_SDF_MAX_FONT_IDS];
    uint32_t next_font_slot;

    /* Complex-script shaping faces for terminal cell runs (owned). Loaded from
     * the bundled TTFs under fonts_dir; the raster shader comes from shaders_dir. */
    char fonts_dir[512];
    struct sdf_shaping_face shaping_faces[YVTERM_SDF_MAX_SHAPING_FACES];
    uint32_t shaping_face_count;

    /* Per-frame transient: wire records, prim table, cell buckets, staging. */
    uint8_t *bytes;
    size_t bytes_len;
    size_t bytes_cap;
    struct sdf_prim_meta *prims;
    uint32_t prim_count;
    uint32_t prim_cap;
    struct sdf_cell *cells;
    uint32_t cell_cols;
    uint32_t cell_rows;
    uint32_t *grid_staging;
    size_t grid_staging_words;
    size_t grid_staging_cap;
    uint32_t *prim_staging;
    size_t prim_staging_words;
    size_t prim_staging_cap;

    /* Render-scoped metrics (set at the top of render, read by bucketing). */
    float cur_cell_w;
    float cur_cell_h;
    uint32_t cur_cols;
    uint32_t cur_rows;
    int32_t cur_rolling_row;
};

/*===========================================================================
 * Small growable buffers
 *=========================================================================*/

static struct yetty_ycore_void_result grow_bytes(struct yetty_yvterm_sdf_layer *layer, size_t need)
{
    if (layer->bytes_len + need <= layer->bytes_cap) {
        return YETTY_OK_VOID();
    }
    size_t cap = layer->bytes_cap ? layer->bytes_cap * 2u : 256u;
    while (layer->bytes_len + need > cap) {
        cap *= 2u;
    }
    uint8_t *grown = (uint8_t *)realloc(layer->bytes, cap);
    if (!grown) {
        return YETTY_ERR(yetty_ycore_void, "sdf-layer: byte buffer oom");
    }
    layer->bytes = grown;
    layer->bytes_cap = cap;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result ensure_words(uint32_t **buf, size_t *cap, size_t want)
{
    if (*cap >= want) {
        return YETTY_OK_VOID();
    }
    size_t new_cap = *cap ? *cap : 64u;
    while (new_cap < want) {
        new_cap *= 2u;
    }
    uint32_t *grown = (uint32_t *)realloc(*buf, new_cap * sizeof(uint32_t));
    if (!grown) {
        return YETTY_ERR(yetty_ycore_void, "sdf-layer: staging buffer oom");
    }
    *buf = grown;
    *cap = new_cap;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result cell_push(struct sdf_cell *cell, uint32_t prim_index)
{
    if (cell->count == cell->cap) {
        uint32_t cap = cell->cap ? cell->cap * 2u : 8u;
        uint32_t *grown = (uint32_t *)realloc(cell->indices, cap * sizeof(uint32_t));
        if (!grown) {
            return YETTY_ERR(yetty_ycore_void, "sdf-layer: cell index oom");
        }
        cell->indices = grown;
        cell->cap = cap;
    }
    cell->indices[cell->count++] = prim_index;
    return YETTY_OK_VOID();
}

/* (Re)allocate the cell grid to cols*rows and zero every bucket count. */
static struct yetty_ycore_void_result cells_reset(struct yetty_yvterm_sdf_layer *layer,
                                                  uint32_t cols, uint32_t rows)
{
    if (cols != layer->cell_cols || rows != layer->cell_rows) {
        size_t old = (size_t)layer->cell_cols * (size_t)layer->cell_rows;
        for (size_t i = 0; i < old; ++i) {
            free(layer->cells[i].indices);
        }
        free(layer->cells);
        layer->cells = NULL;
        layer->cell_cols = 0;
        layer->cell_rows = 0;
        size_t count = (size_t)cols * (size_t)rows;
        if (count > 0) {
            layer->cells = (struct sdf_cell *)calloc(count, sizeof(struct sdf_cell));
            if (!layer->cells) {
                return YETTY_ERR(yetty_ycore_void, "sdf-layer: cells alloc oom");
            }
        }
        layer->cell_cols = cols;
        layer->cell_rows = rows;
        return YETTY_OK_VOID();
    }
    size_t count = (size_t)cols * (size_t)rows;
    for (size_t i = 0; i < count; ++i) {
        layer->cells[i].count = 0;
    }
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Shader assembly (SDF lib + layer code + font dispatcher) — mirrors ygrid
 *=========================================================================*/

static void init_uniforms(struct yetty_yrender_gpu_resource_set *rs)
{
    rs->uniform_count = U_COUNT;
    rs->uniforms[U_GRID_SIZE] =
        (struct yetty_yrender_uniform){"ydraw_grid_size", YETTY_YRENDER_UNIFORM_VEC2};
    rs->uniforms[U_CELL_SIZE] =
        (struct yetty_yrender_uniform){"ydraw_cell_size", YETTY_YRENDER_UNIFORM_VEC2};
    rs->uniforms[U_ROLLING_ROW_0] =
        (struct yetty_yrender_uniform){"ydraw_rolling_row_0", YETTY_YRENDER_UNIFORM_U32};
    rs->uniforms[U_PRIM_COUNT] =
        (struct yetty_yrender_uniform){"ydraw_drawable_count", YETTY_YRENDER_UNIFORM_U32};
    rs->uniforms[U_VZ_SCALE] =
        (struct yetty_yrender_uniform){"ydraw_visual_zoom_scale", YETTY_YRENDER_UNIFORM_F32};
    rs->uniforms[U_VZ_OFF] =
        (struct yetty_yrender_uniform){"ydraw_visual_zoom_off", YETTY_YRENDER_UNIFORM_VEC2};
    rs->uniforms[U_CZ_SCALE] =
        (struct yetty_yrender_uniform){"ydraw_cell_zoom_scale", YETTY_YRENDER_UNIFORM_F32};
    rs->uniforms[U_CZ_OFF] =
        (struct yetty_yrender_uniform){"ydraw_cell_zoom_off", YETTY_YRENDER_UNIFORM_VEC2};
    rs->uniforms[U_VIEW_SIZE] =
        (struct yetty_yrender_uniform){"ydraw_view_size", YETTY_YRENDER_UNIFORM_VEC2};

    rs->uniforms[U_ROLLING_ROW_0].u32 = 0;
    rs->uniforms[U_VZ_SCALE].f32 = 1.0f;
    rs->uniforms[U_VZ_OFF].vec2[0] = 0.0f;
    rs->uniforms[U_VZ_OFF].vec2[1] = 0.0f;
    rs->uniforms[U_CZ_SCALE].f32 = 1.0f;
    rs->uniforms[U_CZ_OFF].vec2[0] = 0.0f;
    rs->uniforms[U_CZ_OFF].vec2[1] = 0.0f;
    rs->uniforms[U_VIEW_SIZE].vec2[0] = 0.0f;
    rs->uniforms[U_VIEW_SIZE].vec2[1] = 0.0f;
}

static struct yetty_ycore_void_result load_sdf_lib(struct yetty_yvterm_sdf_layer *layer)
{
    char path[768];
    snprintf(path, sizeof(path), "%s/ysdf.gen.wgsl", layer->shaders_dir);
    struct yetty_ycore_buffer_result fr = yetty_ycore_read_file(path);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, fr, "sdf-layer: read ysdf.gen.wgsl");
    layer->sdf_lib_code = fr.value;
    strncpy(layer->sdf_lib_rs.namespace, "ysdf_lib", YETTY_YRENDER_NAME_MAX - 1);
    yetty_yrender_shader_code_set(&layer->sdf_lib_rs.shader, (const char *)layer->sdf_lib_code.data,
                                  layer->sdf_lib_code.size);
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result load_layer_shader(struct yetty_yvterm_sdf_layer *layer)
{
    char path[768];
    snprintf(path, sizeof(path), "%s/yvterm-sdf-layer.wgsl", layer->shaders_dir);
    struct yetty_ycore_buffer_result fr = yetty_ycore_read_file(path);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, fr, "sdf-layer: read yvterm-sdf-layer.wgsl");
    layer->layer_shader_code = fr.value;
    return YETTY_OK_VOID();
}

/* Regenerate the combined shader (font dispatcher + layer code) and refresh
 * rs.children with the SDF lib + active fonts. The shader-code hash change
 * triggers a binder refinalize on the next update(). */
static struct yetty_ycore_void_result rebuild_font_dispatcher(struct yetty_yvterm_sdf_layer *layer)
{
    const struct yetty_yrender_gpu_resource_set *font_rs[YETTY_YRENDER_RS_MAX_CHILDREN] = {0};
    const char *slot_namespaces[YETTY_YRENDER_RS_MAX_CHILDREN] = {0};
    for (uint32_t slot = 0; slot < layer->font_count; slot++) {
        if (!layer->fonts[slot]) {
            continue;
        }
        struct yetty_yrender_gpu_resource_set_result font_rs_result =
            layer->fonts[slot]->ops->get_gpu_resource_set(layer->fonts[slot]);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, font_rs_result,
                            "sdf-layer: font get_gpu_resource_set");
        font_rs[slot] = font_rs_result.value;
        slot_namespaces[slot] = font_rs_result.value->namespace;
    }

    char *dispatcher_wgsl = NULL;
    size_t dispatcher_size = 0;
    struct yetty_ycore_void_result dispatcher_result = yetty_yrender_build_font_dispatcher_wgsl(
        slot_namespaces, layer->font_count, &dispatcher_wgsl, &dispatcher_size);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatcher_result, "sdf-layer: build font dispatcher");

    size_t combined_size = dispatcher_size + layer->layer_shader_code.size;
    char *combined_buffer = (char *)malloc(combined_size + 1u);
    if (!combined_buffer) {
        free(dispatcher_wgsl);
        return YETTY_ERR(yetty_ycore_void, "sdf-layer: combined shader oom");
    }
    memcpy(combined_buffer, dispatcher_wgsl, dispatcher_size);
    memcpy(combined_buffer + dispatcher_size, layer->layer_shader_code.data,
           layer->layer_shader_code.size);
    combined_buffer[combined_size] = '\0';
    free(dispatcher_wgsl);

    free(layer->combined_shader);
    layer->combined_shader = combined_buffer;
    layer->combined_shader_size = combined_size;
    yetty_yrender_shader_code_set(&layer->rs.shader, layer->combined_shader,
                                  layer->combined_shader_size);

    size_t children_used = 0;
    layer->rs.children[children_used++] = &layer->sdf_lib_rs;
    for (uint32_t slot = 0; slot < layer->font_count; slot++) {
        if (!font_rs[slot]) {
            continue;
        }
        if (children_used >= YETTY_YRENDER_RS_MAX_CHILDREN) {
            break;
        }
        layer->rs.children[children_used++] =
            (struct yetty_yrender_gpu_resource_set *)font_rs[slot];
    }
    layer->rs.children_count = children_used;
    layer->last_emitted_font_generation = layer->font_generation;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result build_binder(struct yetty_yvterm_sdf_layer *layer)
{
    strncpy(layer->rs.namespace, "ydraw", YETTY_YRENDER_NAME_MAX - 1);
    layer->rs.buffer_count = 2;
    strncpy(layer->rs.buffers[0].name, "grid", YETTY_YRENDER_NAME_MAX - 1);
    strncpy(layer->rs.buffers[0].wgsl_type, "array<u32>", YETTY_YRENDER_WGSL_TYPE_MAX - 1);
    layer->rs.buffers[0].readonly = 1;
    strncpy(layer->rs.buffers[1].name, "prims", YETTY_YRENDER_NAME_MAX - 1);
    strncpy(layer->rs.buffers[1].wgsl_type, "array<u32>", YETTY_YRENDER_WGSL_TYPE_MAX - 1);
    layer->rs.buffers[1].readonly = 1;

    init_uniforms(&layer->rs);
    layer->rs.instance_count = 1;

    struct yetty_ycore_void_result dispatcher_result = rebuild_font_dispatcher(layer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatcher_result, "sdf-layer: initial dispatcher");

    struct yetty_yrender_gpu_resource_binder_result binder_result =
        yetty_yrender_gpu_resource_binder_create(layer->device, layer->queue, layer->target_format,
                                                 layer->allocator);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, binder_result, "sdf-layer: binder create");
    layer->binder = binder_result.value;
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Font management
 *=========================================================================*/

static struct yetty_ycore_void_result sdf_set_font(struct yetty_yvterm_sdf_layer *layer,
                                                   uint32_t slot, struct yetty_yfont_font *font)
{
    if (slot >= YETTY_YRENDER_RS_MAX_CHILDREN - 1u) {
        return YETTY_ERR(yetty_ycore_void, "sdf-layer: font slot out of range");
    }
    layer->fonts[slot] = font;
    if (slot + 1u > layer->font_count) {
        layer->font_count = slot + 1u;
    }
    layer->font_generation++;
    return YETTY_OK_VOID();
}

static uint64_t sdf_fnv1a64(const uint8_t *data, size_t len)
{
    uint64_t hash = 1469598103934665603ULL;
    for (size_t i = 0; i < len; i++) {
        hash ^= data[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

/* Materialise a wire-shipped font into a fresh slot keyed by the producer's
 * envelope-local font_id. Mirrors ygrid's wire-font path. Idempotent per
 * font_id (a record re-seen on a later frame is skipped). */
static struct yetty_ycore_void_result sdf_install_wire_font(
    struct yetty_yvterm_sdf_layer *layer, const struct yetty_ydraw_font_resource_view *fv)
{
    if (fv->font_id < 0 || fv->font_id >= YVTERM_SDF_MAX_FONT_IDS) {
        return YETTY_OK_VOID();
    }
    if (!layer->font_cache) {
        return YETTY_OK_VOID();
    }
    if (layer->wire_font_slot[fv->font_id] >= 0) {
        return YETTY_OK_VOID();
    }

    const char *cache_dir = layer->cache_dir;
    if (!cache_dir || !*cache_dir) {
        return YETTY_ERR(yetty_ycore_void, "sdf-layer wire font: no cache dir");
    }

    char cache_key[128];
    char cdb_path[1024];
    if (fv->ttf_len == 0 && fv->name_len != 16) {
        /* Pre-installed named font (e.g. a music font). Missing CDB → drop. */
        if (fv->name_len == 0 || fv->name_len >= sizeof(cache_key)) {
            return YETTY_OK_VOID();
        }
        memcpy(cache_key, fv->name, fv->name_len);
        cache_key[fv->name_len] = '\0';
        const char *data_dir = layer->data_dir;
        if (!data_dir || !*data_dir) {
            return YETTY_OK_VOID();
        }
        snprintf(cdb_path, sizeof(cdb_path), "%s/msdf-fonts/%s.cdb", data_dir, cache_key);
        if (!yetty_yplatform_file_exists(cdb_path)) {
            return YETTY_OK_VOID();
        }
    } else if (fv->ttf_len == 0) {
        /* Hash-ref: name carries the 16-hex hash of a font cached earlier. */
        memcpy(cache_key, fv->name, 16);
        cache_key[16] = '\0';
        snprintf(cdb_path, sizeof(cdb_path), "%s/ydraw-fonts/pdf_%s.cdb", cache_dir, cache_key);
    } else {
        snprintf(cache_key, sizeof(cache_key), "%016llx",
                 (unsigned long long)sdf_fnv1a64(fv->ttf, fv->ttf_len));
        char fonts_dir[768], ttf_path[1024];
        snprintf(fonts_dir, sizeof(fonts_dir), "%s/ydraw-fonts", cache_dir);
        snprintf(ttf_path, sizeof(ttf_path), "%s/pdf_%s.ttf", fonts_dir, cache_key);
        snprintf(cdb_path, sizeof(cdb_path), "%s/pdf_%s.cdb", fonts_dir, cache_key);
        if (!yetty_yplatform_file_exists(cdb_path)) {
            if (!layer->msdf_generator) {
                return YETTY_ERR(yetty_ycore_void, "sdf-layer wire font: no MSDF generator");
            }
            yetty_yplatform_mkdir_p(fonts_dir);
            if (!yetty_yplatform_file_exists(ttf_path)) {
                FILE *out = fopen(ttf_path, "wb");
                if (!out) {
                    return YETTY_ERR(yetty_ycore_void, "sdf-layer wire font: open ttf cache");
                }
                size_t written = fwrite(fv->ttf, 1, fv->ttf_len, out);
                if (fclose(out) != 0 || written != fv->ttf_len) {
                    return YETTY_ERR(yetty_ycore_void, "sdf-layer wire font: write ttf cache");
                }
            }
            struct yetty_ymsdf_generator_config gen = {
                .ttf_path = ttf_path,
                .cdb_path = cdb_path,
                .font_size = 32.0f,
                .pixel_range = 4.0f,
            };
            struct yetty_ycore_void_result gr =
                layer->msdf_generator->ops->generate(layer->msdf_generator, &gen);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, gr, "sdf-layer wire font: msdf generate");
        }
    }

    struct yetty_yfont_cache_ref_result ref =
        yetty_yfont_cache_get_font(layer->font_cache, cache_key, cdb_path);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, ref, "sdf-layer wire font: cache get_font");

    uint32_t slot = layer->next_font_slot;
    if (slot >= YETTY_YRENDER_RS_MAX_CHILDREN - 1u) {
        yetty_yfont_cache_release_font(layer->font_cache, ref.value.handle);
        return YETTY_ERR(yetty_ycore_void, "sdf-layer wire font: out of font slots");
    }
    struct yetty_ycore_void_result sr = sdf_set_font(layer, slot, ref.value.font);
    if (YETTY_IS_ERR(sr)) {
        yetty_yfont_cache_release_font(layer->font_cache, ref.value.handle);
        return YETTY_ERR(yetty_ycore_void, "sdf-layer wire font: set_font", sr);
    }
    layer->wire_font_slot[fv->font_id] = (int32_t)slot;
    layer->next_font_slot = slot + 1u;
    ydebug("sdf-layer: installed wire font_id=%d -> slot=%u key=%s", fv->font_id, slot, cache_key);
    return YETTY_OK_VOID();
}

/* Install the built-in MSDF default font at slot 0. Text records that ride the
 * canvas default (font_id < 0) — markdown, charts, diagrams, any ydraw producer
 * that ships no FONT resource of its own — resolve to slot 0; without a font
 * there, expand_text_span drops every glyph silently (the bug that left ycat
 * README/markdown blank while ypdf, which embeds its own font into slots 1.. via
 * sdf_install_wire_font, rendered fine). This is the same default font the
 * terminal installs at slot 0 of every ygrid figure. Best-effort: a missing CDB
 * leaves slot 0 empty so only default-font text fails to render; the terminal
 * stays up. */
static struct yetty_ycore_void_result sdf_install_default_font(struct yetty_yvterm_sdf_layer *layer,
                                                               const char *fonts_dir)
{
    if (!layer->font_cache || !fonts_dir || !*fonts_dir) {
        return YETTY_OK_VOID();
    }
    const char *font_family = "DejaVuSansMNerdFontMono";
    char cdb_path[1024];
    snprintf(cdb_path, sizeof(cdb_path), "%s/../msdf-fonts/%s-Regular.cdb", fonts_dir, font_family);
    if (!yetty_yplatform_file_exists(cdb_path)) {
        ydebug("sdf-layer: default font CDB missing (%s) — default-font text will not render",
               cdb_path);
        return YETTY_OK_VOID();
    }

    struct yetty_yfont_cache_ref_result ref =
        yetty_yfont_cache_get_font(layer->font_cache, "ydraw_default", cdb_path);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, ref, "sdf-layer: default font cache get_font");

    struct yetty_ycore_void_result sr = sdf_set_font(layer, 0u, ref.value.font);
    if (YETTY_IS_ERR(sr)) {
        yetty_yfont_cache_release_font(layer->font_cache, ref.value.handle);
        return YETTY_ERR(yetty_ycore_void, "sdf-layer: default font set_font", sr);
    }
    ydebug("sdf-layer: default font installed at slot 0 (%s)", cdb_path);
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Bucketing + indexing (one frame's worth)
 *=========================================================================*/

/* Insert a prim into every SCREEN cell its visible extent overlaps. The
 * prim's local AABB is shifted down by its anchor row; rows/cols outside the
 * viewport are clipped. */
static struct yetty_ycore_void_result bucket_prim(struct yetty_yvterm_sdf_layer *layer,
                                                  uint32_t prim_index)
{
    const struct sdf_prim_meta *prim = &layer->prims[prim_index];
    float cell_w = layer->cur_cell_w;
    float cell_h = layer->cur_cell_h;
    if (cell_w <= 0.0f || cell_h <= 0.0f) {
        return YETTY_OK_VOID();
    }
    int col_min = (int)(prim->min_x / cell_w);
    int col_max = (int)(prim->max_x / cell_w);
    int row_min = prim->rolling_row + (int)(prim->min_y / cell_h);
    int row_max = prim->rolling_row + (int)(prim->max_y / cell_h);
    if (col_min < 0) {
        col_min = 0;
    }
    if (row_min < 0) {
        row_min = 0;
    }
    if (col_max >= (int)layer->cur_cols) {
        col_max = (int)layer->cur_cols - 1;
    }
    if (row_max >= (int)layer->cur_rows) {
        row_max = (int)layer->cur_rows - 1;
    }
    if (col_max < col_min || row_max < row_min) {
        return YETTY_OK_VOID();
    }
    for (int row = row_min; row <= row_max; ++row) {
        for (int col = col_min; col <= col_max; ++col) {
            struct yetty_ycore_void_result pr =
                cell_push(&layer->cells[(size_t)row * layer->cur_cols + (size_t)col], prim_index);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, pr, "sdf-layer: cell_push");
        }
    }
    return YETTY_OK_VOID();
}

/* Append one drawable record (SDF shape or GLYPH) to the frame's byte buffer,
 * compute its AABB, record metadata anchored at cur_rolling_row, and bucket it.
 * FONT and TEXT_DRAWABLE_LIST are handled by the caller before this. */
static struct yetty_ycore_void_result index_record(struct yetty_yvterm_sdf_layer *layer,
                                                   const uint32_t *words, uint32_t word_count)
{
    uint32_t type = words[0];
    struct rectangle_result aabb;

    if (type == YVTERM_SDF_GLYPH_TYPE) {
        if (word_count < 7u) {
            return YETTY_OK_VOID();
        }
        float glyph_x = *(const float *)&words[2];
        float glyph_y = *(const float *)&words[3];
        float glyph_font_size = *(const float *)&words[4];
        float quad_w = glyph_font_size;
        float quad_h = glyph_font_size;
        uint32_t packed = words[5];
        uint32_t slot_plus_one = packed >> 16;
        uint32_t glyph_idx = packed & 0xFFFFu;
        if (slot_plus_one > 0u) {
            uint32_t slot = slot_plus_one - 1u;
            if (slot < layer->font_count && layer->fonts[slot]) {
                struct yetty_yfont_font *font = layer->fonts[slot];
                float base = font->ops->get_base_size(font);
                float scale = (base > 0.0f) ? glyph_font_size / base : 1.0f;
                struct yetty_yrender_gpu_resource_set_result rs_r =
                    font->ops->get_gpu_resource_set(font);
                if (YETTY_IS_OK(rs_r) && rs_r.value->buffer_count > 0 &&
                    rs_r.value->buffers[0].data) {
                    const float *meta = (const float *)rs_r.value->buffers[0].data;
                    uint32_t meta_count =
                        (uint32_t)(rs_r.value->buffers[0].size / (6u * sizeof(float)));
                    if (glyph_idx < meta_count) {
                        quad_w = meta[glyph_idx * 6u + 0u] * scale;
                        quad_h = meta[glyph_idx * 6u + 1u] * scale;
                    }
                } else if (YETTY_IS_ERR(rs_r)) {
                    yetty_ycore_error_destroy(rs_r.error);
                }
            }
        }
        aabb = YETTY_OK(rectangle, ((struct yetty_ycore_rectangle){
                                       .min = {.x = glyph_x, .y = glyph_y},
                                       .max = {.x = glyph_x + quad_w, .y = glyph_y + quad_h},
                                   }));
    } else if (yetty_ydraw_is_composite(type)) {
        /* Composites render through vterm's own rich pass — not here. */
        return YETTY_OK_VOID();
    } else {
        struct yetty_ydraw_drawable_list_entry_ops_ptr_result ops_r = yetty_ysdf_handler(type);
        if (YETTY_IS_ERR(ops_r)) {
            yetty_ycore_error_destroy(ops_r.error); /* unsupported type — render nothing */
            return YETTY_OK_VOID();
        }
        aabb = ops_r.value->aabb(words);
        if (YETTY_IS_ERR(aabb)) {
            yetty_ycore_error_destroy(aabb.error);
            return YETTY_OK_VOID();
        }
    }

    size_t record_bytes = (size_t)word_count * sizeof(uint32_t);
    struct yetty_ycore_void_result gr = grow_bytes(layer, record_bytes);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, gr, "sdf-layer: index grow_bytes");
    uint32_t offset = (uint32_t)layer->bytes_len;
    memcpy(layer->bytes + layer->bytes_len, words, record_bytes);
    layer->bytes_len += record_bytes;

    if (layer->prim_count == layer->prim_cap) {
        uint32_t cap = layer->prim_cap ? layer->prim_cap * 2u : 64u;
        struct sdf_prim_meta *grown =
            (struct sdf_prim_meta *)realloc(layer->prims, cap * sizeof(struct sdf_prim_meta));
        if (!grown) {
            return YETTY_ERR(yetty_ycore_void, "sdf-layer: prims table oom");
        }
        layer->prims = grown;
        layer->prim_cap = cap;
    }
    struct sdf_prim_meta *meta = &layer->prims[layer->prim_count];
    meta->payload_offset = offset;
    meta->payload_words = word_count;
    meta->type = type;
    meta->min_x = aabb.value.min.x;
    meta->min_y = aabb.value.min.y;
    meta->max_x = aabb.value.max.x;
    meta->max_y = aabb.value.max.y;
    meta->rolling_row = layer->cur_rolling_row;
    return bucket_prim(layer, layer->prim_count++);
}

/* Decode one UTF-8 codepoint, advancing *cursor. Returns 0 at end. */
static uint32_t decode_utf8(const uint8_t **cursor_ptr, const uint8_t *end)
{
    const uint8_t *cursor = *cursor_ptr;
    if (cursor >= end) {
        return 0;
    }
    uint8_t lead = *cursor;
    uint32_t codepoint;
    if ((lead & 0x80u) == 0u) {
        codepoint = lead;
        cursor += 1;
    } else if ((lead & 0xE0u) == 0xC0u) {
        codepoint = (uint32_t)(lead & 0x1Fu) << 6;
        cursor += 1;
        if (cursor < end) {
            codepoint |= (uint32_t)(*cursor++ & 0x3Fu);
        }
    } else if ((lead & 0xF0u) == 0xE0u) {
        codepoint = (uint32_t)(lead & 0x0Fu) << 12;
        cursor += 1;
        if (cursor < end) {
            codepoint |= (uint32_t)(*cursor++ & 0x3Fu) << 6;
        }
        if (cursor < end) {
            codepoint |= (uint32_t)(*cursor++ & 0x3Fu);
        }
    } else if ((lead & 0xF8u) == 0xF0u) {
        codepoint = (uint32_t)(lead & 0x07u) << 18;
        cursor += 1;
        if (cursor < end) {
            codepoint |= (uint32_t)(*cursor++ & 0x3Fu) << 12;
        }
        if (cursor < end) {
            codepoint |= (uint32_t)(*cursor++ & 0x3Fu) << 6;
        }
        if (cursor < end) {
            codepoint |= (uint32_t)(*cursor++ & 0x3Fu);
        }
    } else {
        codepoint = 0xFFFDu;
        cursor += 1;
    }
    *cursor_ptr = cursor;
    return codepoint;
}

/* Placement + colour for a shaped run, in the layer's line-local space. Shared
 * by the ydraw text span and the terminal cell scan. */
struct sdf_shaped_run_style {
    uint32_t layer_id;  /* z / layer index stored in the record */
    uint32_t color;     /* packed glyph colour */
    float font_size;    /* target render size, px */
    float base_size;    /* the shaping face's atlas base size, px */
    float char_spacing; /* extra px added after each glyph */
    float baseline_y;   /* text baseline, line-local px */
};

/* Shape codepoints[] with `font` and emit one free-position glyph record per
 * shaped glyph, walking the pen from *cursor_x_ptr. Each shaped glyph id
 * resolves to an atlas slot through the (glyph_id, face) path. HarfBuzz emits
 * glyphs in visual order for LTR and RTL alike, so the left-to-right pen walk is
 * correct regardless of direction. */
static struct yetty_ycore_void_result emit_shaped_glyphs(struct yetty_yvterm_sdf_layer *layer,
                                                         struct yetty_yfont_font *font,
                                                         uint32_t slot, const uint32_t *codepoints,
                                                         size_t count,
                                                         const struct sdf_shaped_run_style *style,
                                                         float *cursor_x_ptr)
{
    float scale = (style->base_size > 0.0f) ? style->font_size / style->base_size : 1.0f;

    enum { SHAPE_RUN_MAX = 256 };
    struct yetty_yfont_shaped_glyph shaped[SHAPE_RUN_MAX];
    struct uint32_result shape_res =
        font->ops->shape_run(font, codepoints, count, shaped, SHAPE_RUN_MAX);
    if (YETTY_IS_ERR(shape_res)) {
        /* Skip this run rather than abort the caller. */
        yetty_ycore_error_destroy(shape_res.error);
        return YETTY_OK_VOID();
    }
    uint32_t glyph_count = shape_res.value;

    float cursor_x = *cursor_x_ptr;
    for (uint32_t gi = 0; gi < glyph_count; gi++) {
        struct uint32_result slot_res = font->ops->get_glyph_index_by_gid(font, shaped[gi].gid);
        if (YETTY_IS_ERR(slot_res)) {
            yetty_ycore_error_destroy(slot_res.error);
            cursor_x += shaped[gi].x_advance * scale + style->char_spacing;
            continue;
        }
        uint32_t glyph_index = slot_res.value;

        struct yetty_yrender_gpu_resource_set_result fresh_rs_result =
            font->ops->get_gpu_resource_set(font);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, fresh_rs_result, "sdf-layer: shaped run font rs");
        const struct yetty_yrender_gpu_resource_set *fresh_rs = fresh_rs_result.value;
        if (fresh_rs->buffer_count == 0 || !fresh_rs->buffers[0].data) {
            return YETTY_ERR(yetty_ycore_void, "sdf-layer: shaped run font has no glyph metadata");
        }
        const float *meta = (const float *)fresh_rs->buffers[0].data;
        uint32_t meta_count = (uint32_t)(fresh_rs->buffers[0].size / (6u * sizeof(float)));
        if (glyph_index >= meta_count) {
            cursor_x += shaped[gi].x_advance * scale + style->char_spacing;
            continue;
        }
        const float *glyph_meta = meta + glyph_index * 6u;
        float size_x = glyph_meta[0];
        float size_y = glyph_meta[1];
        float bearing_x = glyph_meta[2];
        float bearing_y = glyph_meta[3];

        if (size_x > 0.0f && size_y > 0.0f) {
            float glyph_x = cursor_x + (bearing_x + shaped[gi].x_offset) * scale;
            float glyph_y = style->baseline_y - (bearing_y + shaped[gi].y_offset) * scale;

            uint32_t glyph_record[7];
            glyph_record[0] = YVTERM_SDF_GLYPH_TYPE;
            glyph_record[1] = style->layer_id;
            memcpy(&glyph_record[2], &glyph_x, sizeof(float));
            memcpy(&glyph_record[3], &glyph_y, sizeof(float));
            memcpy(&glyph_record[4], &style->font_size, sizeof(float));
            glyph_record[5] = (glyph_index & 0xFFFFu) | ((slot + 1u) << 16);
            glyph_record[6] = style->color;

            struct yetty_ycore_void_result ir = index_record(layer, glyph_record, 7u);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, ir, "sdf-layer: shaped run index glyph");
        }

        cursor_x += shaped[gi].x_advance * scale + style->char_spacing;
    }
    *cursor_x_ptr = cursor_x;
    return YETTY_OK_VOID();
}

/* [ydraw path] Gather a maximal same-script run from the UTF-8 span starting at
 * *cursor_ptr, then shape + emit it. Advances *cursor_ptr past the run and
 * *cursor_x_ptr by the run's total advance. */
static struct yetty_ycore_void_result expand_shaped_run(
    struct yetty_yvterm_sdf_layer *layer, const struct yetty_ydraw_text_drawable_list_view *span,
    struct yetty_yfont_font *font, uint32_t slot, enum yetty_yfont_shaping_script script,
    const uint8_t **cursor_ptr, const uint8_t *end, float *cursor_x_ptr)
{
    /* A single shaped run rarely exceeds a line; a longer contiguous run is
     * shaped in chunks (losing only cross-chunk joining at the boundary). */
    enum { SHAPE_RUN_MAX = 256 };
    uint32_t run_codepoints[SHAPE_RUN_MAX];
    size_t run_count = 0;

    const uint8_t *cursor = *cursor_ptr;
    while (cursor < end && run_count < SHAPE_RUN_MAX) {
        const uint8_t *save = cursor;
        uint32_t codepoint = decode_utf8(&cursor, end);
        if (codepoint == 0) {
            cursor = save;
            break;
        }
        if (yetty_yfont_shaping_script_for_codepoint(codepoint) != script) {
            cursor = save; /* run ended — leave this codepoint for the caller */
            break;
        }
        run_codepoints[run_count++] = codepoint;
    }
    *cursor_ptr = cursor;

    struct sdf_shaped_run_style style = {
        .layer_id = span->layer,
        .color = span->color,
        .font_size = span->font_size,
        .base_size = font->ops->get_base_size(font),
        .char_spacing = span->char_spacing,
        .baseline_y = span->y,
    };
    return emit_shaped_glyphs(layer, font, slot, run_codepoints, run_count, &style, cursor_x_ptr);
}

#ifdef YETTY_ENABLE_LIB_HARFBUZZ
/* Map a codepoint to the bundled TTF that shapes its script. NULL for scripts
 * we do not shape — the grid renders those per-codepoint as before. */
static const char *shaping_font_file_for_codepoint(uint32_t codepoint)
{
    if ((codepoint >= 0x0600 && codepoint <= 0x06FF) ||
        (codepoint >= 0x0750 && codepoint <= 0x077F) ||
        (codepoint >= 0x08A0 && codepoint <= 0x08FF) ||
        (codepoint >= 0xFB50 && codepoint <= 0xFDFF) ||
        (codepoint >= 0xFE70 && codepoint <= 0xFEFF)) {
        return "NotoNaskhArabic-Regular.ttf";
    }
    if (codepoint >= 0x0700 && codepoint <= 0x074F) {
        return "NotoSansSyriac-Regular.ttf";
    }
    if (codepoint >= 0x0900 && codepoint <= 0x097F) {
        return "NotoSansDevanagari-Regular.ttf";
    }
    if (codepoint >= 0x0980 && codepoint <= 0x09FF) {
        return "NotoSansBengali-Regular.ttf";
    }
    if (codepoint >= 0x0A00 && codepoint <= 0x0A7F) {
        return "NotoSansGurmukhi-Regular.ttf";
    }
    if (codepoint >= 0x0A80 && codepoint <= 0x0AFF) {
        return "NotoSansGujarati-Regular.ttf";
    }
    if (codepoint >= 0x0B00 && codepoint <= 0x0B7F) {
        return "NotoSansOriya-Regular.ttf";
    }
    if (codepoint >= 0x0B80 && codepoint <= 0x0BFF) {
        return "NotoSansTamil-Regular.ttf";
    }
    if (codepoint >= 0x0C00 && codepoint <= 0x0C7F) {
        return "NotoSansTelugu-Regular.ttf";
    }
    if (codepoint >= 0x0C80 && codepoint <= 0x0CFF) {
        return "NotoSansKannada-Regular.ttf";
    }
    if (codepoint >= 0x0D00 && codepoint <= 0x0D7F) {
        return "NotoSansMalayalam-Regular.ttf";
    }
    if (codepoint >= 0x0D80 && codepoint <= 0x0DFF) {
        return "NotoSansSinhala-Regular.ttf";
    }
    if (codepoint >= 0x0E00 && codepoint <= 0x0E7F) {
        return "NotoSansThai-Regular.ttf";
    }
    if (codepoint >= 0x0E80 && codepoint <= 0x0EFF) {
        return "NotoSansLao-Regular.ttf";
    }
    if (codepoint >= 0x1000 && codepoint <= 0x109F) {
        return "NotoSansMyanmar-Regular.ttf";
    }
    if (codepoint >= 0x1780 && codepoint <= 0x17FF) {
        return "NotoSansKhmer-Regular.ttf";
    }
    return NULL;
}

/* Load (or return the cached) raster shaping face for the bundled TTF named
 * `file`. Returns NULL when the font is not staged, the shaper is unavailable,
 * or the layer's font slots are exhausted. Loaded faces persist across frames
 * and are installed into a font slot, so the dispatcher/binder pick them up on
 * the frame's rebuild. */
static struct sdf_shaping_face *sdf_face_load_or_get(struct yetty_yvterm_sdf_layer *layer,
                                                     const char *file)
{
    for (uint32_t i = 0; i < layer->shaping_face_count; i++) {
        if (strcmp(layer->shaping_faces[i].file, file) == 0) {
            return layer->shaping_faces[i].font ? &layer->shaping_faces[i] : NULL;
        }
    }
    if (layer->shaping_face_count >= YVTERM_SDF_MAX_SHAPING_FACES || !layer->fonts_dir[0] ||
        layer->next_font_slot >= YETTY_YRENDER_RS_MAX_CHILDREN - 1u) {
        return NULL;
    }

    struct sdf_shaping_face *face = &layer->shaping_faces[layer->shaping_face_count++];
    snprintf(face->file, sizeof(face->file), "%s", file);
    face->font = NULL;
    face->attempted = 1;

    char ttf_path[1024];
    snprintf(ttf_path, sizeof(ttf_path), "%s/%s", layer->fonts_dir, file);
    if (!yetty_yplatform_file_exists(ttf_path)) {
        ydebug("sdf-layer: shaping font not staged: %s", ttf_path);
        return NULL;
    }
    char shader_path[1024];
    snprintf(shader_path, sizeof(shader_path), "%s/raster-font.wgsl", layer->shaders_dir);

    /* Each shaping face becomes a child resource set of this layer's binder
     * tree, so it needs a namespace distinct from every other face (and from
     * the MSDF grid fonts). The font slot it will occupy is unique per layer. */
    char face_namespace[32];
    snprintf(face_namespace, sizeof(face_namespace), "shape_slot%u", layer->next_font_slot);

    struct yetty_font_font_result font_res =
        yetty_yfont_raster_font_create_from_file(ttf_path, shader_path, 48.0f, face_namespace);
    if (YETTY_IS_ERR(font_res)) {
        ydebug("sdf-layer: shaping face load failed (%s): %s", file, font_res.error.msg);
        yetty_ycore_error_destroy(font_res.error);
        return NULL;
    }
    if (!font_res.value->ops->shape_run || !font_res.value->ops->get_glyph_index_by_gid) {
        font_res.value->ops->destroy(font_res.value);
        return NULL; /* no shaper in this build — grid keeps the run */
    }

    uint32_t slot = layer->next_font_slot;
    struct yetty_ycore_void_result set_res = sdf_set_font(layer, slot, font_res.value);
    if (YETTY_IS_ERR(set_res)) {
        yetty_ycore_error_destroy(set_res.error);
        font_res.value->ops->destroy(font_res.value);
        return NULL;
    }
    layer->next_font_slot = slot + 1u;
    face->font = font_res.value;
    face->slot = slot;
    ydebug("sdf-layer: installed shaping face %s -> slot %u", file, slot);
    return face;
}

/* Get (loading on first use) the raster shaping face for a complex-script run
 * beginning with `codepoint`. NULL when the script is not shaped. */
static struct sdf_shaping_face *sdf_shaping_face_for(struct yetty_yvterm_sdf_layer *layer,
                                                     uint32_t codepoint)
{
    const char *file = shaping_font_file_for_codepoint(codepoint);
    if (!file) {
        return NULL;
    }
    return sdf_face_load_or_get(layer, file);
}

/* The bundled programming-ligature face (Fira Code), loaded on the first
 * ligature seen. Staged unconditionally in assets/fonts, so it is present in
 * any build that defines YETTY_ENABLE_LIB_HARFBUZZ. */
static struct sdf_shaping_face *sdf_ligature_face(struct yetty_yvterm_sdf_layer *layer)
{
    return sdf_face_load_or_get(layer, "FiraCode-Regular.ttf");
}

/* Scan one terminal row's cells for complex-script runs and emit shaped glyphs
 * for each over the cells' positions. The grid suppresses those cells' glyphs
 * (vterm_pack_line), so this is the only glyph drawn there. Called per window
 * row during Pass 2, so cur_rolling_row already anchors the row for scroll. */
static struct yetty_ycore_void_result shape_row_cells(struct yetty_yvterm_sdf_layer *layer,
                                                      struct yetty_yclass_object *grid_obj,
                                                      uint32_t slot, float cell_width,
                                                      float cell_height, uint32_t cols)
{
    struct yetty_yvterm_text_cell_const_ptr_result cells_res =
        yetty_yvterm_grid_slot_cells(grid_obj, slot);
    if (YETTY_IS_ERR(cells_res)) {
        yetty_ycore_error_destroy(cells_res.error);
        return YETTY_OK_VOID();
    }
    const struct yetty_yvterm_text_cell *cells = cells_res.value;
    if (!cells) {
        return YETTY_OK_VOID();
    }

    uint32_t col = 0;
    while (col < cols) {
        uint32_t codepoint = cells[col].codepoint;
        enum yetty_yfont_shaping_script script =
            codepoint ? yetty_yfont_shaping_script_for_codepoint(codepoint)
                      : YETTY_YFONT_SHAPING_NONE;
        if (script == YETTY_YFONT_SHAPING_NONE) {
            col++;
            continue;
        }

        struct sdf_shaping_face *face = sdf_shaping_face_for(layer, codepoint);

        /* Gather the maximal same-script run's codepoints (base + marks). */
        enum { CELL_RUN_MAX = 256 };
        uint32_t run_codepoints[CELL_RUN_MAX];
        size_t run_count = 0;
        uint32_t run_start = col;
        while (col < cols && run_count < CELL_RUN_MAX) {
            uint32_t cell_cp = cells[col].codepoint;
            if (!cell_cp || yetty_yfont_shaping_script_for_codepoint(cell_cp) != script) {
                break;
            }
            if (cells[col].width == 0) {
                col++; /* spill cell of a wide glyph — no own codepoint */
                continue;
            }
            run_codepoints[run_count++] = cell_cp;
            for (uint8_t m = 0; m < cells[col].mark_count && run_count < CELL_RUN_MAX; m++) {
                run_codepoints[run_count++] = cells[col].marks[m];
            }
            col++;
        }

        if (!face || run_count == 0) {
            continue; /* no face → grid still draws the run (not suppressed) */
        }

        struct sdf_shaped_run_style style = {
            .layer_id = 0u,
            .color = 0xFFFFFFFFu, /* white; per-cell fg colour is a follow-up */
            .font_size = cell_height,
            .base_size = face->font->ops->get_base_size(face->font),
            .char_spacing = 0.0f,
            .baseline_y = cell_height * 0.80f,
        };
        float cursor_x = (float)run_start * cell_width;
        struct yetty_ycore_void_result er = emit_shaped_glyphs(
            layer, face->font, face->slot, run_codepoints, run_count, &style, &cursor_x);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, er, "sdf-layer: shape row cells");
    }
    return YETTY_OK_VOID();
}

/* Scan one terminal row's cells for programming-ligature spans (=>, !=, ===)
 * and draw each as a shaped glyph over its cells with the Fira Code face. The
 * grid suppresses the same spans (vterm_pack_line calls the identical
 * yetty_yvterm_ligature_run_length), so this is the only glyph drawn there.
 * Complex-script cells never match the ASCII-only ligature table, so the two
 * shaping passes cover disjoint cells. */
static struct yetty_ycore_void_result shape_row_ligatures(struct yetty_yvterm_sdf_layer *layer,
                                                          struct yetty_yclass_object *grid_obj,
                                                          uint32_t slot, float cell_width,
                                                          float cell_height, uint32_t cols)
{
    struct yetty_yvterm_text_cell_const_ptr_result cells_res =
        yetty_yvterm_grid_slot_cells(grid_obj, slot);
    if (YETTY_IS_ERR(cells_res)) {
        yetty_ycore_error_destroy(cells_res.error);
        return YETTY_OK_VOID();
    }
    const struct yetty_yvterm_text_cell *cells = cells_res.value;
    if (!cells) {
        return YETTY_OK_VOID();
    }

    struct sdf_shaping_face *face = NULL; /* loaded lazily on the first ligature */

    uint32_t col = 0;
    while (col < cols) {
        size_t ligature_len = yetty_yvterm_ligature_run_length(cells, cols, col);
        if (ligature_len < 2u) {
            col++;
            continue;
        }

        if (!face) {
            face = sdf_ligature_face(layer);
        }
        if (!face) {
            /* Ligature font unavailable this frame — the grid already suppressed
             * these cells, so nothing more to try; skip past the span. */
            col += (uint32_t)ligature_len;
            continue;
        }

        uint32_t run_codepoints[YETTY_YFONT_LIGATURE_MAX_LEN];
        for (size_t offset = 0; offset < ligature_len; offset++) {
            run_codepoints[offset] = cells[col + offset].codepoint;
        }

        /* Scale the monospace ligature face so one character advance equals the
         * grid cell width: the ligature's own advance then spans exactly its
         * cells, and any non-ligated fallback glyphs stay cell-aligned too. */
        float base_size = face->font->ops->get_base_size(face->font);
        float advance_base = base_size * 0.6f;
        struct float_result adv_res =
            face->font->ops->get_advance(face->font, (uint32_t)'M', base_size);
        if (YETTY_IS_OK(adv_res) && adv_res.value > 0.0f) {
            advance_base = adv_res.value;
        }
        float font_size = cell_width * base_size / advance_base;

        struct sdf_shaped_run_style style = {
            .layer_id = 0u,
            .color = (cells[col].fg & 0x00FFFFFFu) | 0xFF000000u,
            .font_size = font_size,
            .base_size = base_size,
            .char_spacing = 0.0f,
            .baseline_y = cell_height * 0.75f,
        };
        float cursor_x = (float)col * cell_width;
        struct yetty_ycore_void_result er = emit_shaped_glyphs(
            layer, face->font, face->slot, run_codepoints, ligature_len, &style, &cursor_x);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, er, "sdf-layer: shape row ligatures");

        col += (uint32_t)ligature_len;
    }
    return YETTY_OK_VOID();
}
#endif /* YETTY_ENABLE_LIB_HARFBUZZ */

/* Expand a TEXT_DRAWABLE_LIST into one GLYPH record per codepoint, anchored at
 * cur_rolling_row, and index each. Mirrors ygrid's expand_text_span. */
static struct yetty_ycore_void_result expand_text_span(
    struct yetty_yvterm_sdf_layer *layer, const struct yetty_ydraw_text_drawable_list_view *span)
{
    uint32_t slot;
    if (span->font_id >= 0 && span->font_id < YVTERM_SDF_MAX_FONT_IDS &&
        layer->wire_font_slot[span->font_id] >= 0) {
        slot = (uint32_t)layer->wire_font_slot[span->font_id];
    } else {
        slot = (span->font_id < 0) ? 0u : (uint32_t)span->font_id;
    }
    if (slot >= layer->font_count || !layer->fonts[slot]) {
        return YETTY_OK_VOID(); /* no font for this span — dropped, like ygrid */
    }
    struct yetty_yfont_font *font = layer->fonts[slot];

    float base_size = font->ops->get_base_size(font);
    float scale = (base_size > 0.0f) ? span->font_size / base_size : 1.0f;
    float cursor_x = span->x;

    const uint8_t *cursor = (const uint8_t *)span->text;
    const uint8_t *end = cursor + span->text_len;
    while (cursor < end) {
        /* Peek the next codepoint to classify it without consuming. */
        const uint8_t *peek = cursor;
        uint32_t codepoint = decode_utf8(&peek, end);
        if (codepoint == 0) {
            break;
        }

        /* Complex-script run: hand a maximal same-class run to the shaper and
         * emit its shaped glyphs (contextual joining, reordering, mark
         * positioning) through the same free-position glyph records. Falls back
         * to the per-codepoint path when the backend has no shaper (NULL op). */
        enum yetty_yfont_shaping_script script =
            yetty_yfont_shaping_script_for_codepoint(codepoint);
        if (script != YETTY_YFONT_SHAPING_NONE && font->ops->shape_run &&
            font->ops->get_glyph_index_by_gid) {
            struct yetty_ycore_void_result shaped_res =
                expand_shaped_run(layer, span, font, slot, script, &cursor, end, &cursor_x);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, shaped_res, "sdf-layer: shaped run");
            continue;
        }

        /* Simple codepoint: consume it and take the fast per-glyph path. */
        cursor = peek;
        struct uint32_result glyph_idx_result = font->ops->get_glyph_index(font, codepoint);
        if (YETTY_IS_ERR(glyph_idx_result)) {
            yetty_ycore_error_destroy(glyph_idx_result.error);
            cursor_x += (span->font_size * 0.25f) + span->char_spacing;
            if (codepoint == 0x20) {
                cursor_x += span->word_spacing;
            }
            continue;
        }
        uint32_t glyph_index = glyph_idx_result.value;

        struct yetty_yrender_gpu_resource_set_result fresh_rs_result =
            font->ops->get_gpu_resource_set(font);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, fresh_rs_result, "sdf-layer: text_span font rs");
        const struct yetty_yrender_gpu_resource_set *fresh_rs = fresh_rs_result.value;
        if (fresh_rs->buffer_count == 0 || !fresh_rs->buffers[0].data) {
            return YETTY_ERR(yetty_ycore_void, "sdf-layer: text_span font has no glyph metadata");
        }
        const float *meta = (const float *)fresh_rs->buffers[0].data;
        uint32_t meta_count = (uint32_t)(fresh_rs->buffers[0].size / (6u * sizeof(float)));
        if (glyph_index >= meta_count) {
            return YETTY_ERR(yetty_ycore_void, "sdf-layer: text_span glyph_index out of range");
        }
        const float *glyph_meta = meta + glyph_index * 6u;
        float size_x = glyph_meta[0];
        float size_y = glyph_meta[1];
        float bearing_x = glyph_meta[2];
        float bearing_y = glyph_meta[3];
        float advance = glyph_meta[4];

        if (size_x <= 0.0f || size_y <= 0.0f) {
            cursor_x += advance * scale + span->char_spacing;
            if (codepoint == 0x20) {
                cursor_x += span->word_spacing;
            }
            continue;
        }

        float glyph_x = cursor_x + bearing_x * scale;
        float glyph_y = span->y - bearing_y * scale;

        uint32_t glyph_record[7];
        glyph_record[0] = YVTERM_SDF_GLYPH_TYPE;
        glyph_record[1] = span->layer;
        memcpy(&glyph_record[2], &glyph_x, sizeof(float));
        memcpy(&glyph_record[3], &glyph_y, sizeof(float));
        memcpy(&glyph_record[4], &span->font_size, sizeof(float));
        glyph_record[5] = (glyph_index & 0xFFFFu) | ((slot + 1u) << 16);
        glyph_record[6] = span->color;

        struct yetty_ycore_void_result ir = index_record(layer, glyph_record, 7u);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, ir, "sdf-layer: text_span index glyph");

        cursor_x += advance * scale + span->char_spacing;
        if (codepoint == 0x20) {
            cursor_x += span->word_spacing;
        }
    }
    return YETTY_OK_VOID();
}

/* Route one stored wire record by type: register fonts, expand text runs, or
 * index a drawable for rendering. */
static struct yetty_ycore_void_result dispatch_record(struct yetty_yvterm_sdf_layer *layer,
                                                      const uint32_t *words, uint32_t word_count)
{
    uint32_t type = words[0];
    if (type == YETTY_YDRAW_RESOURCE_FONT) {
        struct yetty_ydraw_font_resource_view fv;
        if (yetty_ydraw_font_resource_parse(words, &fv) != 0) {
            return YETTY_OK_VOID();
        }
        struct yetty_ycore_void_result install = sdf_install_wire_font(layer, &fv);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, install, "sdf-layer: install wire font");
        return YETTY_OK_VOID();
    }
    if (type == YETTY_YDRAW_TYPE_TEXT_DRAWABLE_LIST) {
        struct yetty_ydraw_text_drawable_list_view view;
        if (yetty_ydraw_text_drawable_list_parse(words, &view) != 0) {
            return YETTY_OK_VOID();
        }
        return expand_text_span(layer, &view);
    }
    return index_record(layer, words, word_count);
}

/*===========================================================================
 * Staging rebuild
 *=========================================================================*/

static struct yetty_ycore_void_result rebuild_grid_staging(struct yetty_yvterm_sdf_layer *layer)
{
    size_t num_cells = (size_t)layer->cur_cols * (size_t)layer->cur_rows;
    size_t need = num_cells + 1u; /* +1 sentinel block (count=0) */
    for (size_t i = 0; i < num_cells; ++i) {
        if (layer->cells[i].count > 0) {
            need += 1u + layer->cells[i].count;
        }
    }
    struct yetty_ycore_void_result r =
        ensure_words(&layer->grid_staging, &layer->grid_staging_cap, need);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "sdf-layer: grid_staging ensure_words");

    uint32_t sentinel_off = (uint32_t)num_cells;
    layer->grid_staging[sentinel_off] = 0u;
    uint32_t cursor = sentinel_off + 1u;
    for (size_t i = 0; i < num_cells; ++i) {
        const struct sdf_cell *cell = &layer->cells[i];
        if (cell->count == 0) {
            layer->grid_staging[i] = sentinel_off;
            continue;
        }
        layer->grid_staging[i] = cursor;
        layer->grid_staging[cursor++] = cell->count;
        for (uint32_t k = 0; k < cell->count; ++k) {
            layer->grid_staging[cursor++] = cell->indices[k];
        }
    }
    layer->grid_staging_words = need;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result rebuild_prim_staging(struct yetty_yvterm_sdf_layer *layer)
{
    size_t total_record_words = 0;
    for (uint32_t i = 0; i < layer->prim_count; ++i) {
        total_record_words += 1u /* rolling_row */ + layer->prims[i].payload_words;
    }
    size_t need = (size_t)layer->prim_count + total_record_words;
    struct yetty_ycore_void_result r =
        ensure_words(&layer->prim_staging, &layer->prim_staging_cap, need ? need : 1u);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "sdf-layer: prim_staging ensure_words");

    uint32_t cursor = (uint32_t)layer->prim_count; /* offset table first */
    for (uint32_t i = 0; i < layer->prim_count; ++i) {
        const struct sdf_prim_meta *meta = &layer->prims[i];
        layer->prim_staging[i] = cursor - (uint32_t)layer->prim_count;
        /* rolling_row prefix — signed visible row, read with i32() in the
         * shader so scrolling past the prim works in both directions. */
        layer->prim_staging[cursor++] = (uint32_t)meta->rolling_row;
        memcpy(&layer->prim_staging[cursor], layer->bytes + meta->payload_offset,
               (size_t)meta->payload_words * sizeof(uint32_t));
        cursor += meta->payload_words;
    }
    layer->prim_staging_words = need;
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Create / destroy
 *=========================================================================*/

struct yetty_yvterm_sdf_layer_ptr_result yetty_yvterm_sdf_layer_create(
    const struct yetty_context *context)
{
    struct yetty_yvterm_sdf_layer *layer =
        (struct yetty_yvterm_sdf_layer *)calloc(1, sizeof(struct yetty_yvterm_sdf_layer));
    if (!layer) {
        return YETTY_ERR(yetty_yvterm_sdf_layer_ptr, "sdf-layer: alloc oom");
    }
    for (size_t i = 0; i < YVTERM_SDF_MAX_FONT_IDS; ++i) {
        layer->wire_font_slot[i] = -1;
    }
    layer->next_font_slot = 1u;

    if (context == NULL || context->runtime == NULL) {
        layer->headless = 1;
        return YETTY_OK(yetty_yvterm_sdf_layer_ptr, layer);
    }

    layer->device = context->runtime->gpu.device;
    layer->queue = context->runtime->gpu.queue;
    layer->target_format = context->runtime->gpu.surface_format;
    layer->allocator = context->runtime->gpu.allocator;
    layer->msdf_generator = context->runtime->gpu.msdf_generator;
    struct yetty_yconfig_config *config = context->runtime->config;
    const char *shaders_dir = config->ops->get_string(config, "paths/shaders", "");
    snprintf(layer->shaders_dir, sizeof(layer->shaders_dir), "%s", shaders_dir ? shaders_dir : "");
    const char *cache_dir = config->ops->get_string(config, "paths/cache", "");
    snprintf(layer->cache_dir, sizeof(layer->cache_dir), "%s", cache_dir ? cache_dir : "");
    const char *data_dir = config->ops->get_string(config, "paths/data", "");
    snprintf(layer->data_dir, sizeof(layer->data_dir), "%s", data_dir ? data_dir : "");

    struct yetty_yfont_cache_ptr_result font_cache_r = yetty_yfont_cache_create(layer->shaders_dir);
    if (YETTY_IS_OK(font_cache_r)) {
        layer->font_cache = font_cache_r.value;
    } else {
        ydebug("sdf-layer: font cache unavailable: %s", font_cache_r.error.msg);
        yetty_ycore_error_destroy(font_cache_r.error);
    }

    /* Slot 0 = the canvas default font, so font_id<0 text records (markdown,
     * charts, diagrams, …) expand into glyphs instead of being dropped. */
    const char *fonts_dir =
        context->runtime->config->ops->get_string(context->runtime->config, "paths/fonts", "");
    /* Remember the TTF dir so complex-script shaping faces load on demand. */
    snprintf(layer->fonts_dir, sizeof(layer->fonts_dir), "%s", fonts_dir ? fonts_dir : "");
    struct yetty_ycore_void_result df = sdf_install_default_font(layer, fonts_dir);
    if (YETTY_IS_ERR(df)) {
        ydebug("sdf-layer: default font install failed: %s", df.error.msg);
        yetty_ycore_error_destroy(df.error);
    }

    struct yetty_ycore_void_result sl = load_sdf_lib(layer);
    if (YETTY_IS_ERR(sl)) {
        yetty_yvterm_sdf_layer_destroy(layer);
        return YETTY_ERR(yetty_yvterm_sdf_layer_ptr, "sdf-layer: load_sdf_lib", sl);
    }
    struct yetty_ycore_void_result ll = load_layer_shader(layer);
    if (YETTY_IS_ERR(ll)) {
        yetty_yvterm_sdf_layer_destroy(layer);
        return YETTY_ERR(yetty_yvterm_sdf_layer_ptr, "sdf-layer: load_layer_shader", ll);
    }
    struct yetty_ycore_void_result bb = build_binder(layer);
    if (YETTY_IS_ERR(bb)) {
        yetty_yvterm_sdf_layer_destroy(layer);
        return YETTY_ERR(yetty_yvterm_sdf_layer_ptr, "sdf-layer: build_binder", bb);
    }
    return YETTY_OK(yetty_yvterm_sdf_layer_ptr, layer);
}

void yetty_yvterm_sdf_layer_destroy(struct yetty_yvterm_sdf_layer *layer)
{
    if (!layer) {
        return;
    }
    if (layer->binder) {
        layer->binder->ops->destroy(layer->binder);
    }
    /* layer->fonts[] holds cache-owned borrows (wire + system fonts, their
     * MSDF atlases and glyph maps); destroying the cache tears every entry
     * down regardless of refcount. After the binder, which references the
     * fonts' resource sets as children. */
    if (layer->font_cache) {
        yetty_yfont_cache_destroy(layer->font_cache);
    }
    /* Complex-script shaping faces are layer-owned (created directly, not via
     * the cache). Free them after the binder that referenced their rs. Count is
     * 0 in a build without the shaper, so this is a no-op there. */
    for (uint32_t i = 0; i < layer->shaping_face_count; i++) {
        if (layer->shaping_faces[i].font) {
            layer->shaping_faces[i].font->ops->destroy(layer->shaping_faces[i].font);
        }
    }
    size_t cell_count = (size_t)layer->cell_cols * (size_t)layer->cell_rows;
    for (size_t i = 0; i < cell_count; ++i) {
        free(layer->cells[i].indices);
    }
    free(layer->cells);
    free(layer->prims);
    free(layer->bytes);
    free(layer->grid_staging);
    free(layer->prim_staging);
    free(layer->combined_shader);
    free(layer->sdf_lib_code.data);
    free(layer->layer_shader_code.data);
    free(layer);
}

/*===========================================================================
 * Render
 *=========================================================================*/

struct yetty_ycore_void_result yetty_yvterm_sdf_layer_render(
    struct yetty_yvterm_sdf_layer *layer, struct yetty_yclass_object *grid_obj,
    struct yetty_ydraw_target *target, struct yetty_ycore_rectangle rect, float cell_width,
    float cell_height, uint32_t cols, uint32_t rows, const uint32_t *window_slots,
    uint32_t window_rows, uint32_t slot_count, float visual_zoom_scale, float visual_zoom_off_x,
    float visual_zoom_off_y, float cell_zoom_scale)
{
    if (!layer || layer->headless || !layer->binder) {
        return YETTY_OK_VOID();
    }
    if (cols == 0 || rows == 0 || slot_count == 0 || cell_width <= 0.0f || cell_height <= 0.0f ||
        !window_slots || window_rows == 0) {
        return YETTY_OK_VOID();
    }

    /* Reset the frame's transient state. */
    layer->bytes_len = 0;
    layer->prim_count = 0;
    struct yetty_ycore_void_result cr = cells_reset(layer, cols, rows);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, cr, "sdf-layer: cells_reset");
    layer->cur_cell_w = cell_width;
    layer->cur_cell_h = cell_height;
    layer->cur_cols = cols;
    layer->cur_rows = rows;

    /* Pass 1 — install every FONT record FIRST. Records are visited per-line,
     * which is not emission order, so a text run can sit before the FONT it
     * references; installing all fonts up front guarantees the glyph expansion
     * in pass 2 always finds its font. The walk covers the whole hot ring plus
     * the resolved window (archive rows served from the tier cache): a font is
     * installed while its record is hot and the layer's font table persists,
     * so archived text keeps resolving; the window walk covers records that
     * archived before this layer ever rendered them. */
    for (uint32_t walk = 0; walk < slot_count + window_rows; ++walk) {
        uint32_t slot = walk < slot_count ? walk : window_slots[walk - slot_count];
        struct yetty_ycore_uint32_result prim_count_res =
            yetty_yvterm_grid_slot_primitive_count(grid_obj, slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, prim_count_res,
                            "sdf-layer: slot primitive count (fonts)");
        uint32_t prim_count = prim_count_res.value;
        for (uint32_t prim = 0; prim < prim_count; ++prim) {
            uint32_t word_count = 0;
            struct yetty_ycore_const_uint32_ptr_result words_res =
                yetty_yvterm_grid_slot_primitive_words(grid_obj, slot, prim, &word_count);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, words_res,
                                "sdf-layer: slot primitive words (fonts)");
            const uint32_t *words = words_res.value;
            if (!words || word_count == 0 || words[0] != YETTY_YDRAW_RESOURCE_FONT) {
                continue;
            }
            struct yetty_ydraw_font_resource_view fv;
            if (yetty_ydraw_font_resource_parse(words, &fv) != 0) {
                continue;
            }
            struct yetty_ycore_void_result install = sdf_install_wire_font(layer, &fv);
            if (YETTY_IS_ERR(install)) {
                ydebug("sdf-layer: wire font install failed: %s", install.error.msg);
                yetty_ycore_error_destroy(install.error);
            }
        }
    }

    /* Pass 2 — index the drawables (SDF shapes, glyphs, expanded text runs)
     * of the resolved window. Window row r IS the viewport row: a block is
     * anchored on its BOTTOM line (a bottom just below the viewport is inside
     * the window's look-ahead tail); the block's top row, where its local
     * coordinates are anchored, is (bottom − (span − 1)). The shader's
     * rolling-row offset places it at that top, so the block draws top-down
     * from where its text sits while staying owned by its bottom line. */
    for (uint32_t window_row = 0; window_row < window_rows; ++window_row) {
        uint32_t slot = window_slots[window_row];
        struct yetty_ycore_uint32_result prim_count_res =
            yetty_yvterm_grid_slot_primitive_count(grid_obj, slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, prim_count_res, "sdf-layer: slot primitive count");
        uint32_t prim_count = prim_count_res.value;
        struct yetty_ycore_uint32_result span_res = yetty_yvterm_grid_slot_span(grid_obj, slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, span_res, "sdf-layer: slot span");
        uint32_t span = span_res.value;
        layer->cur_rolling_row = (int)window_row - (int)(span ? span - 1u : 0u);
        for (uint32_t prim = 0; prim < prim_count; ++prim) {
            uint32_t word_count = 0;
            struct yetty_ycore_const_uint32_ptr_result words_res =
                yetty_yvterm_grid_slot_primitive_words(grid_obj, slot, prim, &word_count);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, words_res, "sdf-layer: slot primitive words");
            const uint32_t *words = words_res.value;
            if (!words || word_count == 0 || words[0] == YETTY_YDRAW_RESOURCE_FONT) {
                continue; /* fonts already installed in pass 1 */
            }
            struct yetty_ycore_void_result dr = dispatch_record(layer, words, word_count);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, dr, "sdf-layer: dispatch_record");
        }
#ifdef YETTY_ENABLE_LIB_HARFBUZZ
        /* Shape any complex-script runs in this row's terminal cells. The grid
         * suppresses those cells' own glyphs, so this draws them shaped. */
        struct yetty_ycore_void_result shape_res =
            shape_row_cells(layer, grid_obj, slot, cell_width, cell_height, cols);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, shape_res, "sdf-layer: shape row cells");
        /* Draw programming ligatures (=>, !=, ===) for this row's cells; the
         * grid suppresses the same spans (identical ligature-run decision). */
        struct yetty_ycore_void_result lig_res =
            shape_row_ligatures(layer, grid_obj, slot, cell_width, cell_height, cols);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, lig_res, "sdf-layer: shape row ligatures");
#endif
    }
    ydebug("sdf-layer: render prim_count=%u font_count=%u rows=%u cols=%u", layer->prim_count,
           layer->font_count, rows, cols);

    if (layer->prim_count == 0) {
        return YETTY_OK_VOID(); /* nothing to draw this frame */
    }

    /* Font set may have changed (a wire FONT was installed this frame) — rebuild
     * the dispatcher so the new font's helpers + rs child are wired in. */
    if (layer->font_generation != layer->last_emitted_font_generation) {
        struct yetty_ycore_void_result dispatcher_result = rebuild_font_dispatcher(layer);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatcher_result, "sdf-layer: rebuild dispatcher");
    }

    struct yetty_ycore_void_result gs = rebuild_grid_staging(layer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, gs, "sdf-layer: grid staging");
    struct yetty_ycore_void_result ps = rebuild_prim_staging(layer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, ps, "sdf-layer: prim staging");

    layer->rs.buffers[0].data = (uint8_t *)layer->grid_staging;
    layer->rs.buffers[0].size = layer->grid_staging_words * sizeof(uint32_t);
    layer->rs.buffers[0].dirty = 1;
    layer->rs.buffers[1].data = (uint8_t *)layer->prim_staging;
    layer->rs.buffers[1].size = layer->prim_staging_words * sizeof(uint32_t);
    layer->rs.buffers[1].dirty = 1;

    float width = rect.max.x - rect.min.x;
    float height = rect.max.y - rect.min.y;
    layer->rs.uniforms[U_GRID_SIZE].vec2[0] = (float)cols;
    layer->rs.uniforms[U_GRID_SIZE].vec2[1] = (float)rows;
    layer->rs.uniforms[U_CELL_SIZE].vec2[0] = cell_width;
    layer->rs.uniforms[U_CELL_SIZE].vec2[1] = cell_height;
    layer->rs.uniforms[U_ROLLING_ROW_0].u32 = 0u;
    layer->rs.uniforms[U_PRIM_COUNT].u32 = layer->prim_count;
    layer->rs.uniforms[U_VIEW_SIZE].vec2[0] = (float)cols * cell_width;
    layer->rs.uniforms[U_VIEW_SIZE].vec2[1] = (float)rows * cell_height;
    /* Zoom: the shader applies the same canonical visual-zoom transform
     * (pane-centred) + structural cell-zoom (around origin) as the text/figure
     * shaders, so SDF drawables scale and pan in lockstep. cz_off stays 0 — the
     * cell-zoom is a pure scale. */
    layer->rs.uniforms[U_VZ_SCALE].f32 = visual_zoom_scale > 0.0f ? visual_zoom_scale : 1.0f;
    layer->rs.uniforms[U_VZ_OFF].vec2[0] = visual_zoom_off_x;
    layer->rs.uniforms[U_VZ_OFF].vec2[1] = visual_zoom_off_y;
    layer->rs.uniforms[U_CZ_SCALE].f32 = cell_zoom_scale > 0.0f ? cell_zoom_scale : 1.0f;
    layer->rs.pixel_size.width = width;
    layer->rs.pixel_size.height = height;

    struct yetty_ycore_void_result sr = layer->binder->ops->submit(layer->binder, &layer->rs);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "sdf-layer: binder submit");
    if (!layer->binder_finalized) {
        struct yetty_ycore_void_result fr = layer->binder->ops->finalize(layer->binder);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, fr, "sdf-layer: binder finalize");
        layer->binder_finalized = 1;
    }
    struct yetty_ycore_void_result ur = layer->binder->ops->update(layer->binder);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, ur, "sdf-layer: binder update");

    WGPUTextureView view = target->ops->get_view(target);
    if (!view) {
        return YETTY_ERR(yetty_ycore_void, "sdf-layer: target view NULL");
    }
    if (width <= 0.0f || height <= 0.0f) {
        return YETTY_OK_VOID();
    }
    float vx = target->viewport.x + rect.min.x;
    float vy = target->viewport.y + rect.min.y;

    WGPUCommandEncoderDescriptor enc_desc = {0};
    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(layer->device, &enc_desc);
    if (!enc) {
        return YETTY_ERR(yetty_ycore_void, "sdf-layer: encoder create");
    }
    WGPURenderPassColorAttachment ca = {0};
    ca.view = view;
    ca.loadOp = WGPULoadOp_Load;
    ca.storeOp = WGPUStoreOp_Store;
    ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    WGPURenderPassDescriptor pass_desc = {0};
    pass_desc.colorAttachmentCount = 1;
    pass_desc.colorAttachments = &ca;
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(enc, &pass_desc);
    wgpuRenderPassEncoderSetViewport(pass, vx, vy, width, height, 0.0f, 1.0f);

    /* Clamp the scissor to the target so a rect slightly past the pane edge
     * (rounding) does not trip the validation layer. */
    float tx0 = target->viewport.x;
    float ty0 = target->viewport.y;
    float tx1 = target->viewport.x + target->viewport.w;
    float ty1 = target->viewport.y + target->viewport.h;
    float sx0 = vx > tx0 ? vx : tx0;
    float sy0 = vy > ty0 ? vy : ty0;
    float sx1 = (vx + width) < tx1 ? (vx + width) : tx1;
    float sy1 = (vy + height) < ty1 ? (vy + height) : ty1;
    if (sx1 <= sx0 || sy1 <= sy0) {
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
        WGPUCommandBufferDescriptor cb_skip_desc = {0};
        WGPUCommandBuffer cb_skip = wgpuCommandEncoderFinish(enc, &cb_skip_desc);
        wgpuQueueSubmit(layer->queue, 1, &cb_skip);
        wgpuCommandBufferRelease(cb_skip);
        wgpuCommandEncoderRelease(enc);
        return YETTY_OK_VOID();
    }
    wgpuRenderPassEncoderSetScissorRect(pass, (uint32_t)sx0, (uint32_t)sy0, (uint32_t)(sx1 - sx0),
                                        (uint32_t)(sy1 - sy0));

    WGPURenderPipeline pipe = layer->binder->ops->get_pipeline(layer->binder);
    WGPUBuffer quad_vb = layer->binder->ops->get_quad_vertex_buffer(layer->binder);
    wgpuRenderPassEncoderSetPipeline(pass, pipe);
    if (quad_vb) {
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, quad_vb, 0, WGPU_WHOLE_SIZE);
    }
    struct yetty_ycore_void_result br = layer->binder->ops->bind(layer->binder, pass, 0);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, br, "sdf-layer: binder bind");
    wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    WGPUCommandBufferDescriptor cb_desc = {0};
    WGPUCommandBuffer cb = wgpuCommandEncoderFinish(enc, &cb_desc);
    wgpuQueueSubmit(layer->queue, 1, &cb);
    wgpuCommandBufferRelease(cb);
    wgpuCommandEncoderRelease(enc);
    return YETTY_OK_VOID();
}
