/*
 * vterm.c — the terminal content as a yfigure: class@yvterm:vterm.
 *
 * The GPU renderer. It is a yclass figure (parent yfigure:figure) that COMPOSES
 * a separate class@yvterm:grid object (the terminal model + libvterm + keyboard
 * I/O, in grid.c). Concerns are split: grid.c owns the truth; this figure reads
 * it through grid's bulk accessors (yetty_yvterm_grid_*) and draws the MSDF text
 * plane plus anchored composites. The public yetty_yvterm_vterm_* model entry
 * points here are thin delegators to the grid object, so the terminal keeps one
 * API surface.
 *
 * The model types (text cell, attrs) come from the generated grid.h. This TU
 * declares its own yetty_yvterm_vterm_ptr_result — the same one vterm.h
 * publishes for consumers — rather than including its own generated header.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <vterm.h>
#include <vterm_keycodes.h>

#include <webgpu/webgpu.h>

#include <yetty/yclass/class.h>
#include <yetty/yconfig/config.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/ycore/util.h>
#include <yetty/yetty/yetty.h>
#include "yetty/gen/impl/yfigure/figure.h"
#include <yetty/yfont/font.h>
#include <yetty/yfont/ms-font.h>
#include <yetty/yfont/ms-msdf-font.h>
#include <yetty/yfont/ms-raster-font.h>
#include <yetty/yfont/msdf-font.h> /* yetty_yfont_msdf_resolve_cdb */
#include <yetty/yframework/yframework.h>
#include <yetty/ymsdf/generator.h>
#include <yetty/yplatform/fs.h>
#include <yetty/yrender/gpu-resource-set.h>
#include <yetty/yrender/render-target.h>
#include <yetty/yrender/texture-format.h>
#include <yetty/api/ytermsink/sink.h>
#include <yetty/ytrace/ytrace.h>
#include <yetty/yfont/shader-glyph.h> /* shader-glyph codepoint table lookup */
#include "yetty/gen/impl/yvterm/grid.h"
#include <yetty/yvterm/shader-glyph-pua.h> /* PUA-B codepoint helpers */
#include <yetty/ywire/wire-statemachine.h>

#include "ligature-cells.h"
#include "sdf-layer.h"
#include "shader-glyph-layer.h"

/* GPU cell layout the text shader reads: 4 u32 words per cell. */
enum { YVTERM_WORDS_PER_CELL = 4 };

/* Sentinel glyph index the text shader draws as a hollow "notdef" box. Set when
 * a printable, spacing codepoint resolves to no glyph in any face — a visible
 * tofu instead of a silently blank cell. Real atlas indices are far below this. */
enum { YVTERM_GLYPH_TOFU = 0xFFFFFFFFu };

/*===========================================================================
 * Multi-font faces — config-driven codepoint-range → font routing.
 *
 * Face 0 is always the base terminal font (font/family, the text layer's
 * render-method). Extra faces come from the terminal/text-layer/font/ranges
 * config list ({from, to, font, render-method} entries) so e.g. CJK ranges
 * can route to a raster Noto face while Latin stays on the MSDF base font.
 * Every face binds its own atlas texture + glyph-meta buffer; the text
 * shader picks the face per cell (packed next to the cell width) and decodes
 * per the face's render method.
 *=========================================================================*/
enum { YVTERM_MAX_FONT_FACES = 4 };
enum { YVTERM_MAX_FONT_RANGES = 64 };
enum { YVTERM_FONT_FACE_NAME_MAX = 64 };

enum yvterm_font_method {
    YVTERM_FONT_METHOD_MSDF = 0,
    YVTERM_FONT_METHOD_RASTER = 1,
    /* Raster face whose atlas is RGBA8 (color emoji) — detected from the
     * created font's atlas format, not spelled in the config (the config
     * still says "raster"). The shader samples pre-colored texels and skips
     * foreground tinting. */
    YVTERM_FONT_METHOD_RASTER_COLOR = 2,
};

struct yvterm_font_face {
    struct yetty_yfont_ms_font *font; /* NULL = unused slot */
    enum yvterm_font_method method;
    char name[YVTERM_FONT_FACE_NAME_MAX];

    /* Per-face GPU copies of the font's atlas + glyph metadata. */
    WGPUBuffer meta_buffer;
    size_t meta_capacity;
    WGPUTexture atlas_texture;
    WGPUTextureView atlas_view;
    uint32_t atlas_width;
    uint32_t atlas_height;
    uint32_t atlas_format;    /* WGPUTextureFormat */
    uint32_t bytes_per_pixel; /* of atlas_format */
};

/* One codepoint range routed to a face. Matched in config order. */
struct yvterm_font_range {
    uint32_t from;
    uint32_t to;
    uint32_t face;
};

/* How many rows BELOW the visible bottom the rich pass scans for figure anchors,
 * on top of one screen height. A figure is anchored on its BOTTOM line but spans
 * upward, so in a scrolled-back view a figure whose bottom sits just below the
 * viewport may still have its top poking into the pane. The scan therefore looks
 * ahead past the bottom far enough to find the anchor of any figure taller than
 * the screen; the look-ahead is additionally capped by the live scroll distance
 * (no figure exists below the live bottom). Past this a figure is fully off the
 * bottom. The per-figure extent test culls anything that doesn't reach. */
enum { YVTERM_COMPOSITE_ANCHOR_LOOKAHEAD_ROWS = 256 };

/* Must match the Uniforms struct in the text shader (text_wgsl below). */
struct vterm_uniforms {
    float grid_size[2];
    float cell_size[2];
    float scale;
    float baseline_y;
    float glyph_left;
    float pixel_range;
    uint32_t root_row;
    uint32_t cursor_col;
    uint32_t cursor_row;
    uint32_t cursor_visible;
    /* Selection highlight, normalised to reading order (start <= end). */
    uint32_t sel_active;
    uint32_t sel_start_row;
    uint32_t sel_start_col;
    uint32_t sel_end_row;
    uint32_t sel_end_col;
    /* Full line-ring size (visible + scrollback) — the shader's slot modulo is
     * over this, so a scrolled-back root_row addresses retained history rows. */
    uint32_t ring_rows;
    /* Visual (non-intrusive) zoom: magnify the rendered grid without changing
     * the cell grid. The fragment shader inverts this (grid_px = (screen_px -
     * offset) / scale) so text scales and pans in exact lockstep with anchored
     * figures, which apply the identical scale+offset to their pixel origin. */
    float visual_zoom_scale;
    float visual_zoom_offset_x;
    float visual_zoom_offset_y;
    /* Shared animation clock, pointer positions, and the two OSC-driven effect
     * selections (post-color + coordinate distortion), each index 0 = none
     * with 6 params. Laid out to stay 16-byte aligned (160 bytes total). */
    float time;
    float mouse_x; /* pixels within the pane; falls back to the cursor centre */
    float mouse_y;
    uint32_t post_fx_index;
    float post_fx_p0;
    float post_fx_p1;
    float post_fx_p2;
    float post_fx_p3;
    float post_fx_p4;
    float post_fx_p5;
    uint32_t coord_fx_index;
    float coord_fx_p0;
    float coord_fx_p1;
    float coord_fx_p2;
    float coord_fx_p3;
    float coord_fx_p4;
    float coord_fx_p5;
    uint32_t pad_a;
    uint32_t pad_b;
    /* Multi-font faces: render method per face (4 bits each, low nibble =
     * face 0) and per-face font geometry (pixel_range, scale, baseline_y,
     * glyph_left) — one vec4 per face, 16-byte aligned for WGSL. */
    uint32_t face_methods;
    uint32_t face_pad0;
    uint32_t face_pad1;
    uint32_t face_pad2;
    float face_params[YVTERM_MAX_FONT_FACES][4];
};

/* `struct yetty_ydraw_composite` is forward-declared in grid.c and kept opaque
 * here on purpose: it is defined in BOTH ydraw-core/figure.h and
 * ydraw-factory/composite-factory.h (a pre-existing duplicate), so pulling
 * either defining header would clash in any consumer that includes the other.
 * The factory teardown is declared by hand so this TU only ever uses the type
 * by pointer — which also lets codegen forward-declare it in the generated
 * header rather than including a (conflicting) defining header. */
struct yetty_ydraw_composite;
void yetty_ydraw_composite_destroy(struct yetty_ydraw_composite *instance);
/* Render an anchored composite at a pixel origin — hand-declared free function
 * (defined in ydraw-factory) so this TU can draw composites without including
 * the conflicting defining header. */
struct yetty_ycore_void_result yetty_ydraw_composite_render(struct yetty_ydraw_composite *instance,
                                                            struct yetty_ydraw_target *target,
                                                            float x, float y);
/* Laid-out pixel height of a figure — hand-declared for the same reason. Lets
 * the rich pass keep a figure drawn while any of its rows is still on screen,
 * instead of dropping it the moment its anchor (top) line scrolls off. */
float yetty_ydraw_composite_pixel_height(const struct yetty_ydraw_composite *instance);
float yetty_ydraw_composite_pixel_bottom(const struct yetty_ydraw_composite *instance);
/* Set a figure's content scale so it magnifies with the zoomed text — same
 * reason for the hand declaration. */
void yetty_ydraw_composite_set_content_scale(struct yetty_ydraw_composite *instance, float scale);

/* The render slot takes the render target only by pointer and never derefs it
 * here, so a forward decl suffices — avoids pulling the webgpu-heavy
 * render-target.h into the model TU (matches figure.c's own forward decls). */
struct yetty_ydraw_target;

/* Absolute lowest stacking order: the container sorts children by (z, seq) and
 * renders back-to-front, so the most-negative z renders first (the floor). The
 * terminal content must sit below every other figure. */
#define YETTY_YVTERM_VTERM_Z (-2000000000)

/*===========================================================================
 * The vterm yclass class — a yfigure subclass owning the unified model.
 *=========================================================================*/

struct YETTY_ANNOTATE("class@yvterm:vterm") YETTY_ANNOTATE("parent@yfigure:figure")
    yetty_yvterm_vterm {
    /* The terminal model lives in a separate class@yvterm:grid object; this
     * figure composes it and renders it. Owned: disposed in the destroy slot. */
    struct yetty_yclass_object *grid_obj;

    /* Cell + grid metrics mirrored for the terminal's mouse→cell mapping, PTY
     * resize, and the figure rect. The terminal sets these via resize. */
    struct yetty_ycore_grid_size grid_size;
    struct yetty_ycore_pixel_size cell_size;
    /* Cell height at first sizing — the reference for figure scaling under
     * intrusive (cell-size) zoom. A figure reserves N rows at creation; when the
     * cell later grows, the figure must grow by cell_height/baseline to keep
     * filling those N (now taller) rows. 0 until the first resize. */
    float baseline_cell_height;

    /* Resolved content rect in pane-local pixels — where the text surface sits
     * inside the figure rect (a client reserved part of the pane for its own
     * overlay via YETTY_OSC_CS_CONTENT_RECT / _INSET; the terminal resolves the
     * request against the pane and pushes the result here). width/height <= 0
     * (the default) means the content fills the whole figure rect. */
    float content_rect_x;
    float content_rect_y;
    float content_rect_w;
    float content_rect_h;

    /* The terminal-host sink (ytermsink:sink): request_render is dispatched on
     * it, and it is handed down to the grid + shader-glyph layer for their own
     * upcalls. Borrowed — the terminal owns it and outlives this figure. */
    struct yetty_yclass_object *sink;

    /* Scrollback view (not yet backed by a scrollback ring — see methods). */
    /* Scrollback view state lives on the GRID (single owner); the renderer
     * queries it per frame. Only the repaint marker stays here. */

    /* Renderer-local dirty bit: set when view/zoom state changes (which the grid
     * model knows nothing about) so yetty_yvterm_vterm_is_dirty() still reports a
     * pending repaint. Cleared when the frame is rendered. */
    int view_dirty;

    /* Visual zoom (applied by the renderer build-out). */
    float visual_zoom_scale;
    float visual_zoom_offset_x;
    float visual_zoom_offset_y;

    /* ---- GPU text renderer (ported from poc/yvterm-new) ---- */
    /* Borrowed from the framework. */
    struct yetty_yframework *runtime; /* shared animation clock (frame_time_sec) */
    WGPUDevice device;
    WGPUQueue queue;
    WGPUTextureFormat target_format;

    /* Font faces: slot 0 = base font, slots 1+ = config range faces. The
     * codepoint→face routing table is matched in config order at pack time. */
    struct yvterm_font_face faces[YVTERM_MAX_FONT_FACES];
    uint32_t face_count;
    struct yvterm_font_range font_ranges[YVTERM_MAX_FONT_RANGES];
    uint32_t font_range_count;

    /* OSC-driven post-color effect for the (opaque) terminal text surface.
     * index 0 = none; matches effects-lib.wgsl / the OSC protocol numbering. */
    uint32_t post_fx_index;
    float post_fx_params[6];
    /* OSC-driven coordinate distortion (fisheye/barrel/swirl/…). index 0 = none. */
    uint32_t coord_fx_index;
    float coord_fx_params[6];
    /* Live pointer position in pane-local pixels (set on mouse move). Until the
     * pointer first enters the pane, the shader falls back to the cursor cell
     * centre. Mouse-following effects (fisheye, magnify-mouse) use this. */
    float mouse_local_x;
    float mouse_local_y;
    int have_mouse;
    /* Combined shader source = effects-lib.wgsl (or a no-op stub) + the text
     * shader. Owned; the shader module holds no reference after creation. */
    char *combined_shader;
    struct yetty_ycore_buffer effects_lib_code;

    /* Owned wgpu resources. (Per-face atlas/meta live in faces[].) */
    WGPUShaderModule shader_module;
    WGPUBindGroupLayout bind_group_layout;
    WGPURenderPipeline pipeline;
    WGPUSampler sampler;
    WGPUBuffer cell_buffer;
    WGPUBuffer uniform_buffer;
    WGPUBindGroup bind_group;
    int bind_group_valid;
    uint32_t cell_buffer_cells; /* capacity in cells */

    struct vterm_uniforms uniforms;
    uint32_t *row_scratch; /* cols * WORDS_PER_CELL scratch reused per line */
    uint32_t row_scratch_cols;
    int gpu_ready; /* pipeline + font built */

    /* yvterm's own SDF/glyph/text renderer for the raw ydraw records stored on
     * the grid ring (ycat PDF/SVG/markdown). Best-effort: NULL if its shaders
     * couldn't load — text + composites still render. */
    struct yetty_yvterm_sdf_layer *sdf_layer;

    /* Animated procedural "shader glyphs": PUA-B cells (U+100000..U+100FFF)
     * rendered as per-cell fragment shaders on top of the text. Best-effort:
     * NULL if its shaders couldn't load — the rest still renders. */
    struct yetty_yvterm_shader_glyph_layer *shader_glyph_layer;
};

/* Result wrapper for the vterm handle. Declared here (this TU does not include
 * its own generated header); vterm.h publishes the identical declaration. */
YETTY_YRESULT_DECLARE(yetty_yvterm_vterm_ptr, struct yetty_yvterm_vterm *);

/* Defined in the appended vterm.gen.c. */
struct yetty_yclass_ptr_result yetty_yvterm_vterm_class_get(void);
struct yetty_yvterm_vterm_ptr_result yetty_yvterm_vterm_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_yvterm_vterm_to(struct yetty_yvterm_vterm *data);

/*===========================================================================
 * GPU text renderer — ported from poc/yvterm-new. Resolves each cell's glyph
 * against the active MSDF font, packs the 16-byte/cell layout the text shader
 * reads, uploads dirty lines into one pinned storage buffer, and draws a single
 * full-screen quad into the figure's render target. CPU truth stays the model.
 *=========================================================================*/

static WGPUStringView vterm_sv(const char *text)
{
    return (WGPUStringView){.data = text, .length = strlen(text)};
}

/* The text grid shader (mirror of poc/yvterm-new/text.wgsl). Local static so it
 * is program-lifetime storage without a file-scope symbol. */
static const char *vterm_text_wgsl(void)
{
    static const char src[] =
        "struct Uniforms {\n"
        "    grid_size: vec2<f32>,\n"
        "    cell_size: vec2<f32>,\n"
        "    scale: f32,\n"
        "    baseline_y: f32,\n"
        "    glyph_left: f32,\n"
        "    pixel_range: f32,\n"
        "    root_row: u32,\n"
        "    cursor_col: u32,\n"
        "    cursor_row: u32,\n"
        "    cursor_visible: u32,\n"
        "    sel_active: u32,\n"
        "    sel_start_row: u32,\n"
        "    sel_start_col: u32,\n"
        "    sel_end_row: u32,\n"
        "    sel_end_col: u32,\n"
        "    ring_rows: u32,\n"
        "    visual_zoom_scale: f32,\n"
        "    visual_zoom_offset_x: f32,\n"
        "    visual_zoom_offset_y: f32,\n"
        "    time: f32, mouse_x: f32, mouse_y: f32,\n"
        "    post_fx_index: u32,\n"
        "    post_fx_p0: f32, post_fx_p1: f32, post_fx_p2: f32,\n"
        "    post_fx_p3: f32, post_fx_p4: f32, post_fx_p5: f32,\n"
        "    coord_fx_index: u32,\n"
        "    coord_fx_p0: f32, coord_fx_p1: f32, coord_fx_p2: f32,\n"
        "    coord_fx_p3: f32, coord_fx_p4: f32, coord_fx_p5: f32,\n"
        "    pad_a: u32, pad_b: u32,\n"
        "    face_methods: u32, face_pad0: u32, face_pad1: u32, face_pad2: u32,\n"
        "    face_params: array<vec4<f32>, 4>,\n"
        "};\n"
        "@group(0) @binding(0) var<storage, read> cells: array<u32>;\n"
        "@group(0) @binding(1) var<storage, read> glyph_meta: array<u32>;\n"
        "@group(0) @binding(2) var atlas_tex: texture_2d<f32>;\n"
        "@group(0) @binding(3) var atlas_smp: sampler;\n"
        "@group(0) @binding(4) var<uniform> uni: Uniforms;\n"
        /* Extra font faces (config range faces). Unused slots are bound to the
         * face-0 resources, and face_methods routes decoding, so the shader is
         * compiled once regardless of how many faces the config declares. */
        "@group(0) @binding(5) var<storage, read> face1_meta: array<u32>;\n"
        "@group(0) @binding(6) var face1_tex: texture_2d<f32>;\n"
        "@group(0) @binding(7) var<storage, read> face2_meta: array<u32>;\n"
        "@group(0) @binding(8) var face2_tex: texture_2d<f32>;\n"
        "@group(0) @binding(9) var<storage, read> face3_meta: array<u32>;\n"
        "@group(0) @binding(10) var face3_tex: texture_2d<f32>;\n"
        "struct VSOut {\n"
        "    @builtin(position) pos: vec4<f32>,\n"
        "    @location(0) @interpolate(linear) grid_pixel: vec2<f32>,\n"
        "};\n"
        "@vertex\n"
        "fn vs_main(@builtin(vertex_index) vid: u32) -> VSOut {\n"
        "    var corners = array<vec2<f32>, 6>(\n"
        "        vec2<f32>(-1.0,-1.0), vec2<f32>(1.0,-1.0), vec2<f32>(1.0,1.0),\n"
        "        vec2<f32>(-1.0,-1.0), vec2<f32>(1.0,1.0), vec2<f32>(-1.0,1.0));\n"
        "    let ndc = corners[vid];\n"
        "    var out: VSOut;\n"
        "    out.pos = vec4<f32>(ndc, 0.0, 1.0);\n"
        "    let grid_w = uni.grid_size.x * uni.cell_size.x;\n"
        "    let grid_h = uni.grid_size.y * uni.cell_size.y;\n"
        "    out.grid_pixel = vec2<f32>((ndc.x*0.5+0.5)*grid_w, (0.5-ndc.y*0.5)*grid_h);\n"
        "    return out;\n"
        "}\n"
        "fn median3(r: f32, g: f32, b: f32) -> f32 {\n"
        "    return max(min(r,g), min(max(r,g), b));\n"
        "}\n"
        "fn face_meta(face: u32, index: u32) -> u32 {\n"
        "    switch face {\n"
        "        case 1u: { return face1_meta[index]; }\n"
        "        case 2u: { return face2_meta[index]; }\n"
        "        case 3u: { return face3_meta[index]; }\n"
        "        default: { return glyph_meta[index]; }\n"
        "    }\n"
        "}\n"
        "fn face_texel(face: u32, uv: vec2<f32>) -> vec4<f32> {\n"
        "    switch face {\n"
        "        case 1u: { return textureSampleLevel(face1_tex, atlas_smp, uv, 0.0); }\n"
        "        case 2u: { return textureSampleLevel(face2_tex, atlas_smp, uv, 0.0); }\n"
        "        case 3u: { return textureSampleLevel(face3_tex, atlas_smp, uv, 0.0); }\n"
        "        default: { return textureSampleLevel(atlas_tex, atlas_smp, uv, 0.0); }\n"
        "    }\n"
        "}\n"
        "fn face_atlas_size(face: u32) -> vec2<f32> {\n"
        "    switch face {\n"
        "        case 1u: { return vec2<f32>(textureDimensions(face1_tex)); }\n"
        "        case 2u: { return vec2<f32>(textureDimensions(face2_tex)); }\n"
        "        case 3u: { return vec2<f32>(textureDimensions(face3_tex)); }\n"
        "        default: { return vec2<f32>(textureDimensions(atlas_tex)); }\n"
        "    }\n"
        "}\n"
        /* Raster faces: 4-word meta (uv origin + slot width in cells), glyphs
         * pre-rasterized at cell size. Returns the raw texel: R8 coverage
         * lands in .r, color (RGBA8 emoji) texels come through whole. */
        "fn sample_raster_texel(face: u32, glyph: u32, local_px: vec2<f32>) -> vec4<f32> {\n"
        "    let base = glyph * 4u;\n"
        "    let uv0 = vec2<f32>(bitcast<f32>(face_meta(face, base+0u)), "
        "bitcast<f32>(face_meta(face, base+1u)));\n"
        "    if (uv0.x < 0.0) { return vec4<f32>(0.0); }\n"
        "    let width_cells = max(bitcast<f32>(face_meta(face, base+2u)), 1.0);\n"
        "    let slot_px = vec2<f32>(uni.cell_size.x * width_cells, uni.cell_size.y);\n"
        "    if (local_px.x < 0.0 || local_px.y < 0.0 || local_px.x >= slot_px.x || "
        "local_px.y >= slot_px.y) { return vec4<f32>(0.0); }\n"
        "    let uv = uv0 + local_px / face_atlas_size(face);\n"
        "    return face_texel(face, uv);\n"
        "}\n"
        /* One MSDF coverage tap: median of the three channels, mapped to an
         * alpha ramp screen_px_range screen-pixels steep around the 0.5
         * iso-line. */
        "fn msdf_coverage(face: u32, uv: vec2<f32>, screen_px_range: f32) -> f32 {\n"
        "    let texel = face_texel(face, uv);\n"
        "    let sd = median3(texel.r, texel.g, texel.b);\n"
        "    return clamp((sd - 0.5) * screen_px_range + 0.5, 0.0, 1.0);\n"
        "}\n"
        /* MSDF faces: 10-word glyph meta (uv_min, uv_max, size, bearing,
         * advance, pad), face_params = (pixel_range, scale, baseline_y,
         * glyph_left). */
        "fn sample_face_glyph(face: u32, glyph: u32, local_px: vec2<f32>) -> f32 {\n"
        "    let method = (uni.face_methods >> (face * 4u)) & 0xFu;\n"
        "    if (method == 1u) {\n"
        "        return sample_raster_texel(face, glyph, local_px).r;\n"
        "    }\n"
        "    let params = uni.face_params[face];\n"
        "    let base = glyph * 10u;\n"
        "    let uv_min = vec2<f32>(bitcast<f32>(face_meta(face, base+0u)), "
        "bitcast<f32>(face_meta(face, base+1u)));\n"
        "    let uv_max = vec2<f32>(bitcast<f32>(face_meta(face, base+2u)), "
        "bitcast<f32>(face_meta(face, base+3u)));\n"
        "    let gsize = vec2<f32>(bitcast<f32>(face_meta(face, base+4u)), "
        "bitcast<f32>(face_meta(face, base+5u)));\n"
        "    let bear = vec2<f32>(bitcast<f32>(face_meta(face, base+6u)), "
        "bitcast<f32>(face_meta(face, base+7u)));\n"
        "    if (gsize.x <= 0.0 || gsize.y <= 0.0) { return 0.0; }\n"
        "    let scaled_size = gsize * params.y;\n"
        "    let scaled_bear = bear * params.y;\n"
        "    let gtop = params.z - scaled_bear.y;\n"
        "    let gleft = params.w + scaled_bear.x;\n"
        "    let gmin = vec2<f32>(gleft, gtop);\n"
        "    let gmax = vec2<f32>(gleft + scaled_size.x, gtop + scaled_size.y);\n"
        "    if (local_px.x < gmin.x || local_px.x >= gmax.x || local_px.y < gmin.y || "
        "local_px.y >= gmax.y) { return 0.0; }\n"
        "    let gl = (local_px - gmin) / scaled_size;\n"
        "    let uv = mix(uv_min, uv_max, gl);\n"
        /* AA width in SCREEN pixels: field range in atlas texels (params.x)
         * × grid px per texel (params.y) × screen px per grid px (the
         * visual zoom). Without the zoom factor the ramp is 1 grid px, so a
         * zoomed-in glyph edge smears across `zoom` screen pixels. Clamped
         * so deep minification never drops the ramp below one screen px. */
        "    let zoom = max(uni.visual_zoom_scale, 0.0001);\n"
        "    let screen_px_range = max(params.x * params.y * zoom, 1.0);\n"
        "    let texels_per_screen_px = 1.0 / (params.y * zoom);\n"
        "    if (texels_per_screen_px < 1.25) {\n"
        "        return msdf_coverage(face, uv, screen_px_range);\n"
        "    }\n"
        /* Minified: one screen pixel spans >1.25 atlas texels, and a single
         * bilinear tap under-resolves the field (stroke-weight wobble,
         * nicked corners). Box-filter instead: 2x2 taps at ±0.25 screen px,
         * each with a half-pixel ramp, averaged. */
        "    let tap_uv = (uv_max - uv_min) / scaled_size * (0.25 / zoom);\n"
        "    let tap_range = screen_px_range * 2.0;\n"
        "    var coverage = msdf_coverage(face, uv + vec2<f32>(-tap_uv.x, -tap_uv.y), tap_range);\n"
        "    coverage += msdf_coverage(face, uv + vec2<f32>(tap_uv.x, -tap_uv.y), tap_range);\n"
        "    coverage += msdf_coverage(face, uv + vec2<f32>(-tap_uv.x, tap_uv.y), tap_range);\n"
        "    coverage += msdf_coverage(face, uv + vec2<f32>(tap_uv.x, tap_uv.y), tap_range);\n"
        "    return coverage * 0.25;\n"
        "}\n"
        "@fragment\n"
        "fn fs_main(in: VSOut) -> @location(0) vec4<f32> {\n"
        "    let grid_w = uni.grid_size.x * uni.cell_size.x;\n"
        "    let grid_h = uni.grid_size.y * uni.cell_size.y;\n"
        /* Invert the visual-zoom transform to find the grid pixel under this
         * fragment. This is the canonical mouse-anchored zoom shared with the
         * figure shaders and the zoom controller:
         *     source = (screen - center)/scale + center + offset
         * where offset is a SOURCE-space pan (so a drag makes content follow the
         * cursor) and center is the pane centre. Identity at scale 1 / offset 0.
         * Text and figures use the same formula, so they zoom, pan and stay
         * anchored to the same row together. */
        "    let vz = max(uni.visual_zoom_scale, 0.0001);\n"
        "    let center = vec2<f32>(grid_w, grid_h) * 0.5;\n"
        "    let voff = vec2<f32>(uni.visual_zoom_offset_x, uni.visual_zoom_offset_y);\n"
        "    var px = (in.grid_pixel - center) / vz + center + voff;\n"
        /* Pointer positions in pane pixels: mouse (falls back to cursor) and
         * the terminal cursor cell centre. */
        "    let fx_cursor = vec2<f32>((f32(uni.cursor_col) + 0.5) * uni.cell_size.x,\n"
        "                              (f32(uni.cursor_row) + 0.5) * uni.cell_size.y);\n"
        "    let fx_mouse = vec2<f32>(uni.mouse_x, uni.mouse_y);\n"
        /* Apply coordinate distortion after the zoom inversion. */
        "    px = fx_coord_apply(uni.coord_fx_index, px, vec2<f32>(grid_w, grid_h),\n"
        "                        uni.time, fx_mouse, fx_cursor,\n"
        "                        uni.coord_fx_p0, uni.coord_fx_p1, uni.coord_fx_p2,\n"
        "                        uni.coord_fx_p3, uni.coord_fx_p4, uni.coord_fx_p5);\n"
        "    if (px.x < 0.0 || px.y < 0.0 || px.x >= grid_w || px.y >= grid_h) {\n"
        "        return vec4<f32>(0.0,0.0,0.0,1.0);\n"
        "    }\n"
        "    let colf = floor(px.x / uni.cell_size.x);\n"
        "    let rowf = floor(px.y / uni.cell_size.y);\n"
        "    let col = u32(colf);\n"
        "    let row = u32(rowf);\n"
        "    let gcols = u32(uni.grid_size.x);\n"
        "    let slot = (row + uni.root_row) % uni.ring_rows;\n"
        "    let cell_index = slot * gcols + col;\n"
        "    var local = vec2<f32>(px.x - colf*uni.cell_size.x, px.y - rowf*uni.cell_size.y);\n"
        "    var glyph = cells[cell_index*4u + 0u];\n"
        "    let w1 = cells[cell_index*4u + 1u];\n"
        "    let w2 = cells[cell_index*4u + 2u];\n"
        "    let attrs = (w2 >> 16u) & 0xFFFFu;\n"
        "    let fg = vec3<f32>(f32(w1 & 0xFFu)/255.0, f32((w1>>8u)&0xFFu)/255.0, "
        "f32((w1>>16u)&0xFFu)/255.0);\n"
        "    let bg = vec3<f32>(f32((w1>>24u)&0xFFu)/255.0, f32(w2 & 0xFFu)/255.0, "
        "f32((w2>>8u)&0xFFu)/255.0);\n"
        /* A wide glyph occupies two cells: the head (width 2) holds the glyph,
         * the spill cell (width 0) to its right is blank. Continue sampling the
         * head glyph into the spill cell, shifted one cell to the right, so the
         * right half of CJK/double-width glyphs is drawn. */
        "    var face = (cells[cell_index*4u + 3u] >> 8u) & 0xFFu;\n"
        "    if ((cells[cell_index*4u + 3u] & 0xFFu) == 0u && col > 0u) {\n"
        "        let head = slot * gcols + (col - 1u);\n"
        "        if ((cells[head*4u + 3u] & 0xFFu) == 2u) {\n"
        "            glyph = cells[head*4u + 0u];\n"
        "            face = (cells[head*4u + 3u] >> 8u) & 0xFFu;\n"
        "            local.x = local.x + uni.cell_size.x;\n"
        "        }\n"
        "    }\n"
        "    var alpha = 0.0;\n"
        "    var glyph_rgb = vec3<f32>(0.0);\n"
        "    var glyph_is_color = false;\n"
        /* 0xFFFFFFFF = notdef sentinel: a codepoint no face could supply. Draw a
         * hollow box inset from the cell edges (fg-tinted) so a missing glyph is
         * a visible tofu, never a blank cell. */
        "    if (glyph == 0xFFFFFFFFu) {\n"
        "        let inx = uni.cell_size.x * 0.16;\n"
        "        let iny = uni.cell_size.y * 0.12;\n"
        "        let th = max(1.0, uni.cell_size.x * 0.07);\n"
        "        let x0 = inx; let x1 = uni.cell_size.x - inx;\n"
        "        let y0 = iny; let y1 = uni.cell_size.y - iny;\n"
        "        let on_box = local.x >= x0 && local.x <= x1 && local.y >= y0 && local.y <= y1;\n"
        "        let in_hole = local.x >= x0 + th && local.x <= x1 - th &&\n"
        "                      local.y >= y0 + th && local.y <= y1 - th;\n"
        "        if (on_box && !in_hole) { alpha = 1.0; }\n"
        "    } else if (glyph != 0u) {\n"
        "        let cell_method = (uni.face_methods >> (face * 4u)) & 0xFu;\n"
        "        if (cell_method == 2u) {\n"
        "            let texel = sample_raster_texel(face, glyph, local);\n"
        "            glyph_rgb = texel.rgb;\n"
        "            alpha = texel.a;\n"
        "            glyph_is_color = true;\n"
        "        } else {\n"
        "            alpha = sample_face_glyph(face, glyph, local);\n"
        "        }\n"
        "    }\n"
        /* Underline (single 0x2 or double 0x4) sits just below the baseline;
         * strikethrough (0x40) crosses the cell middle. Both paint at full
         * coverage so they show on blank cells too. */
        "    let line_h = max(1.0, uni.cell_size.y * 0.07);\n"
        "    let ul_y = uni.baseline_y + max(1.0, uni.cell_size.y * 0.10);\n"
        "    if ((attrs & 0x6u) != 0u && local.y >= ul_y && local.y < ul_y + line_h) "
        "{ alpha = 1.0; }\n"
        "    let st_y = uni.cell_size.y * 0.5;\n"
        "    if ((attrs & 0x40u) != 0u && local.y >= st_y && local.y < st_y + line_h) "
        "{ alpha = 1.0; }\n"
        "    let is_cursor = uni.cursor_visible != 0u && col == uni.cursor_col && "
        "row == uni.cursor_row;\n"
        /* Selection highlight (reading-order stream, start <= end). Inverted
         * like the cursor — the xterm default look. */
        "    var selected = false;\n"
        "    if (uni.sel_active != 0u) {\n"
        "        if (row > uni.sel_start_row && row < uni.sel_end_row) { selected = true; }\n"
        "        else if (row == uni.sel_start_row && row == uni.sel_end_row) {\n"
        "            selected = col >= uni.sel_start_col && col <= uni.sel_end_col;\n"
        "        } else if (row == uni.sel_start_row) { selected = col >= uni.sel_start_col; }\n"
        "        else if (row == uni.sel_end_row) { selected = col <= uni.sel_end_col; }\n"
        "    }\n"
        "    var composed = mix(bg, fg, alpha);\n"
        "    if (glyph_is_color) {\n" /* pre-colored emoji texel — no fg tint */
        "        composed = mix(bg, glyph_rgb, alpha);\n"
        "    }\n"
        "    if (is_cursor || selected) {\n"
        "        composed = mix(fg, bg, alpha);\n" /* inverted: fg fill, glyph punched in bg */
        "    }\n"
        /* Post-color effect over the opaque terminal surface. Shared clock in
         * uni.time keeps animation in phase with every other shader. */
        "    if (uni.post_fx_index != 0u) {\n"
        "        let screen = uni.grid_size * uni.cell_size;\n"
        "        composed = fx_post_apply(uni.post_fx_index, composed, in.grid_pixel, screen,\n"
        "            uni.time, fx_mouse, fx_cursor, uni.cell_size,\n"
        "            uni.post_fx_p0, uni.post_fx_p1, uni.post_fx_p2,\n"
        "            uni.post_fx_p3, uni.post_fx_p4, uni.post_fx_p5);\n"
        "    }\n"
        "    return vec4<f32>(composed, 1.0);\n"
        "}\n";
    return src;
}

/* Pick the face for a codepoint: first matching config range wins, face 0
 * (the base font) is the default. */
static uint32_t vterm_face_for_codepoint(const struct yetty_yvterm_vterm *vterm, uint32_t codepoint)
{
    for (uint32_t i = 0; i < vterm->font_range_count; ++i) {
        const struct yvterm_font_range *range = &vterm->font_ranges[i];
        if (codepoint >= range->from && codepoint <= range->to) {
            return range->face;
        }
    }
    return 0;
}

/* Resolve a codepoint to an atlas glyph index in the given face for the given
 * style. The styled lookup lazy-loads the (style, codepoint) slot on demand,
 * falling back to the Regular face when a bold/italic variant is absent. */
static uint32_t vterm_resolve_glyph(struct yetty_yvterm_vterm *vterm, uint32_t face_index,
                                    uint32_t codepoint, const uint32_t *marks, uint8_t mark_count,
                                    enum yetty_yfont_ms_style style)
{
    struct yetty_yfont_ms_font *font = vterm->faces[face_index].font;
    if (!font || codepoint == 0) {
        return 0;
    }
    /* A cluster with combining marks resolves to a composited slot when the
     * backend supports it (raster); otherwise the marks are dropped and only
     * the base glyph is drawn (MSDF has no live rasterizer). */
    struct uint32_result gi;
    if (mark_count > 0 && font->ops->get_glyph_index_cluster) {
        gi = font->ops->get_glyph_index_cluster(font, codepoint, marks, mark_count, style);
    } else {
        gi = font->ops->get_glyph_index_styled(font, codepoint, style);
    }
    if (YETTY_IS_OK(gi)) {
        return gi.value;
    }
    yetty_ycore_error_destroy(gi.error);
    return 0;
}

/* Map cell text attributes to a font style variant (bold/italic combinations). */
static enum yetty_yfont_ms_style vterm_cell_style(uint16_t attrs)
{
    int style = YETTY_YFONT_MS_STYLE_REGULAR;
    if (attrs & YETTY_YVTERM_ATTR_BOLD) {
        style |= YETTY_YFONT_MS_STYLE_BOLD;
    }
    if (attrs & YETTY_YVTERM_ATTR_ITALIC) {
        style |= YETTY_YFONT_MS_STYLE_ITALIC;
    }
    return (enum yetty_yfont_ms_style)style;
}

/* Pack one model line (cells[cols], from the grid accessor) into out[cols*4] in
 * the GPU layout the shader reads. */
static void vterm_pack_line(struct yetty_yvterm_vterm *vterm,
                            const struct yetty_yvterm_text_cell *cells, uint32_t cols,
                            uint32_t *out)
{
#ifdef YETTY_ENABLE_LIB_HARFBUZZ
    /* Cells still covered by a programming ligature the SDF layer draws over
     * the grid (see ligature-cells.h). Counts down across the span. */
    uint32_t ligature_remaining = 0u;
#endif
    for (uint32_t col = 0; col < cols; ++col) {
        const struct yetty_yvterm_text_cell *cell = &cells[col];
        /* Concealed cells render their background only — resolve no glyph.
         * Bold/italic pick a styled atlas slot via the cell attributes. */
        uint32_t glyph = 0u;
        uint32_t face = 0u;
        int resolve_glyph = cell->codepoint && !(cell->attrs & YETTY_YVTERM_ATTR_CONCEAL);
#ifdef YETTY_ENABLE_LIB_HARFBUZZ
        if (ligature_remaining > 0u) {
            /* Interior cell of a programming ligature: the SDF layer shapes the
             * whole ligature and draws its glyph on top, so paint background
             * only here — same contract as complex-script / shader-glyph cells. */
            ligature_remaining--;
            resolve_glyph = 0;
        } else if (resolve_glyph) {
            size_t ligature_len = yetty_yvterm_ligature_run_length(cells, cols, col);
            if (ligature_len >= 2u) {
                ligature_remaining = (uint32_t)(ligature_len - 1u);
                resolve_glyph = 0;
            }
        }
#endif
        if (resolve_glyph) {
            /* PUA-B shader-glyph cells are painted by the shader-glyph layer, not
             * the font atlas — leave glyph 0 so the text pass draws only the
             * cell background under the animation. */
            if (yetty_shader_glyph_codepoint_in_range(cell->codepoint) &&
                yetty_yfont_shader_glyph_codepoint_exists(cell->codepoint)) {
                glyph = 0u;
#ifdef YETTY_ENABLE_LIB_HARFBUZZ
            } else if (yetty_yfont_shaping_script_for_codepoint(cell->codepoint) !=
                       YETTY_YFONT_SHAPING_NONE) {
                /* Complex-script cell (Arabic/Indic/Thai/...): the SDF layer
                 * shapes the run and draws it (joining, reordering, mark
                 * positioning). Leave the grid glyph empty so the text pass
                 * paints only the cell background — same as shader-glyph cells. */
                glyph = 0u;
#endif
            } else {
                face = vterm_face_for_codepoint(vterm, cell->codepoint);
                glyph = vterm_resolve_glyph(vterm, face, cell->codepoint, cell->marks,
                                            cell->mark_count, vterm_cell_style(cell->attrs));
                /* A range face that cannot supply the glyph falls back to the
                 * base font rather than leaving the cell blank. */
                if (glyph == 0u && face != 0u) {
                    face = 0u;
                    glyph = vterm_resolve_glyph(vterm, 0u, cell->codepoint, cell->marks,
                                                cell->mark_count, vterm_cell_style(cell->attrs));
                }
                /* Interim combining-mark rendering only composites on a raster
                 * (FreeType) face. When the resolved face is MSDF — which has no
                 * live rasterizer and drops the marks — re-resolve the cluster
                 * against a raster face whose fallback chain covers the base,
                 * preferring one that also covers the marks (a mono fallback
                 * such as the wide "Noto*" glob) over one that only has the base
                 * (e.g. a CJK face's Latin block, which lacks most diacritics).
                 * Rare (most accented text arrives precomposed), so the extra
                 * coverage probes cost nothing in practice. */
                if (cell->mark_count > 0u &&
                    vterm->faces[face].method != YVTERM_FONT_METHOD_RASTER &&
                    vterm->faces[face].method != YVTERM_FONT_METHOD_RASTER_COLOR) {
                    enum yetty_yfont_ms_style mark_style = vterm_cell_style(cell->attrs);
                    uint32_t chosen_face = 0u;
                    int chosen_found = 0;
                    for (uint32_t raster_face = 0u; raster_face < vterm->face_count;
                         ++raster_face) {
                        if (vterm->faces[raster_face].method != YVTERM_FONT_METHOD_RASTER) {
                            continue;
                        }
                        if (vterm_resolve_glyph(vterm, raster_face, cell->codepoint, NULL, 0u,
                                                mark_style) == 0u) {
                            continue; /* this face lacks the base glyph */
                        }
                        chosen_face = raster_face;
                        chosen_found = 1;
                        if (vterm_resolve_glyph(vterm, raster_face, cell->marks[0], NULL, 0u,
                                                mark_style) != 0u) {
                            break; /* covers base AND marks — the best fit */
                        }
                    }
                    if (chosen_found) {
                        glyph = vterm_resolve_glyph(vterm, chosen_face, cell->codepoint,
                                                    cell->marks, cell->mark_count, mark_style);
                        face = chosen_face;
                    }
                }
                /* No face (nor its fallback chain) has this printable, spacing
                 * codepoint: draw a visible notdef box instead of a blank cell.
                 * Zero-width spill/combining cells (width 0) stay invisible. */
                if (glyph == 0u && cell->codepoint >= 0x20u && cell->width >= 1) {
                    glyph = YVTERM_GLYPH_TOFU;
                    face = 0u;
                }
            }
        }
        uint32_t fg = cell->fg;
        uint32_t bg = cell->bg;
        uint32_t fr = fg & 0xFFu, fgr = (fg >> 8) & 0xFFu, fb = (fg >> 16) & 0xFFu;
        uint32_t br = bg & 0xFFu, bgr = (bg >> 8) & 0xFFu, bb = (bg >> 16) & 0xFFu;
        uint32_t *words = &out[(size_t)col * YVTERM_WORDS_PER_CELL];
        words[0] = glyph;
        words[1] = fr | (fgr << 8) | (fb << 16) | (br << 24);
        words[2] = bgr | (bb << 8) | ((uint32_t)cell->attrs << 16);
        words[3] = (uint32_t)cell->width | (face << 8);
    }
}

static struct yetty_ycore_void_result vterm_ensure_row_scratch(struct yetty_yvterm_vterm *vterm,
                                                               uint32_t cols)
{
    if (vterm->row_scratch && vterm->row_scratch_cols >= cols) {
        return YETTY_OK_VOID();
    }
    free(vterm->row_scratch);
    vterm->row_scratch = calloc((size_t)cols * YVTERM_WORDS_PER_CELL, sizeof(uint32_t));
    if (!vterm->row_scratch) {
        vterm->row_scratch_cols = 0;
        return YETTY_ERR(yetty_ycore_void, "vterm: row scratch alloc");
    }
    vterm->row_scratch_cols = cols;
    return YETTY_OK_VOID();
}

/* No-op effect fns used when effects-lib.wgsl is unavailable. Signatures MUST
 * match effects-lib.wgsl so the text shader's calls still resolve. */
static const char *vterm_fx_stub(void)
{
    static const char stub[] =
        "fn fx_post_apply(index: u32, color: vec3<f32>, pixel: vec2<f32>, screen: vec2<f32>, "
        "time: f32, mouse: vec2<f32>, cursor: vec2<f32>, cell: vec2<f32>, p0: f32, p1: f32, "
        "p2: f32, p3: f32, p4: f32, p5: f32) -> vec3<f32> { return color; }\n"
        "fn fx_coord_apply(index: u32, pixel: vec2<f32>, screen: vec2<f32>, time: f32, "
        "mouse: vec2<f32>, cursor: vec2<f32>, p0: f32, p1: f32, p2: f32, p3: f32, p4: f32, "
        "p5: f32) -> vec2<f32> { return pixel; }\n";
    return stub;
}

static struct yetty_ycore_void_result vterm_create_pipeline(struct yetty_yvterm_vterm *vterm)
{
    /* Combined shader = effects library (or stub) + the text shader. */
    const char *lib = (vterm->effects_lib_code.data && vterm->effects_lib_code.size > 0)
                          ? (const char *)vterm->effects_lib_code.data
                          : vterm_fx_stub();
    const char *text = vterm_text_wgsl();
    size_t lib_len = strlen(lib);
    size_t text_len = strlen(text);
    free(vterm->combined_shader);
    vterm->combined_shader = malloc(lib_len + text_len + 2);
    if (!vterm->combined_shader) {
        return YETTY_ERR(yetty_ycore_void, "vterm: combined shader alloc");
    }
    memcpy(vterm->combined_shader, lib, lib_len);
    vterm->combined_shader[lib_len] = '\n';
    memcpy(vterm->combined_shader + lib_len + 1, text, text_len);
    vterm->combined_shader[lib_len + 1 + text_len] = '\0';

    WGPUShaderSourceWGSL wgsl = {0};
    wgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgsl.code = vterm_sv(vterm->combined_shader);
    WGPUShaderModuleDescriptor shader_desc = {0};
    shader_desc.nextInChain = &wgsl.chain;
    shader_desc.label = vterm_sv("yvterm text shader");
    vterm->shader_module = wgpuDeviceCreateShaderModule(vterm->device, &shader_desc);
    if (!vterm->shader_module) {
        return YETTY_ERR(yetty_ycore_void, "vterm: shader module");
    }

    WGPUBindGroupLayoutEntry entries[11] = {0};
    entries[0].binding = 0;
    entries[0].visibility = WGPUShaderStage_Fragment;
    entries[0].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
    entries[1].binding = 1;
    entries[1].visibility = WGPUShaderStage_Fragment;
    entries[1].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
    entries[2].binding = 2;
    entries[2].visibility = WGPUShaderStage_Fragment;
    entries[2].texture.sampleType = WGPUTextureSampleType_Float;
    entries[2].texture.viewDimension = WGPUTextureViewDimension_2D;
    entries[3].binding = 3;
    entries[3].visibility = WGPUShaderStage_Fragment;
    entries[3].sampler.type = WGPUSamplerBindingType_Filtering;
    entries[4].binding = 4;
    entries[4].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    entries[4].buffer.type = WGPUBufferBindingType_Uniform;
    entries[4].buffer.minBindingSize = sizeof(struct vterm_uniforms);
    /* Extra font faces: meta buffer + atlas texture per slot (5/6, 7/8, 9/10). */
    for (uint32_t i = 1; i < YVTERM_MAX_FONT_FACES; ++i) {
        entries[3 + i * 2].binding = 3 + i * 2;
        entries[3 + i * 2].visibility = WGPUShaderStage_Fragment;
        entries[3 + i * 2].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
        entries[4 + i * 2].binding = 4 + i * 2;
        entries[4 + i * 2].visibility = WGPUShaderStage_Fragment;
        entries[4 + i * 2].texture.sampleType = WGPUTextureSampleType_Float;
        entries[4 + i * 2].texture.viewDimension = WGPUTextureViewDimension_2D;
    }
    WGPUBindGroupLayoutDescriptor bgl_desc = {0};
    bgl_desc.entryCount = 11;
    bgl_desc.entries = entries;
    vterm->bind_group_layout = wgpuDeviceCreateBindGroupLayout(vterm->device, &bgl_desc);
    if (!vterm->bind_group_layout) {
        return YETTY_ERR(yetty_ycore_void, "vterm: bind group layout");
    }

    WGPUPipelineLayoutDescriptor pl_desc = {0};
    pl_desc.bindGroupLayoutCount = 1;
    pl_desc.bindGroupLayouts = &vterm->bind_group_layout;
    WGPUPipelineLayout layout = wgpuDeviceCreatePipelineLayout(vterm->device, &pl_desc);
    if (!layout) {
        return YETTY_ERR(yetty_ycore_void, "vterm: pipeline layout");
    }

    WGPUColorTargetState color_target = {0};
    color_target.format = vterm->target_format;
    color_target.writeMask = WGPUColorWriteMask_All;
    WGPUFragmentState fragment = {0};
    fragment.module = vterm->shader_module;
    fragment.entryPoint = vterm_sv("fs_main");
    fragment.targetCount = 1;
    fragment.targets = &color_target;
    WGPURenderPipelineDescriptor rp_desc = {0};
    rp_desc.label = vterm_sv("yvterm pipeline");
    rp_desc.layout = layout;
    rp_desc.vertex.module = vterm->shader_module;
    rp_desc.vertex.entryPoint = vterm_sv("vs_main");
    rp_desc.fragment = &fragment;
    rp_desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    rp_desc.primitive.frontFace = WGPUFrontFace_CCW;
    rp_desc.primitive.cullMode = WGPUCullMode_None;
    rp_desc.multisample.count = 1;
    rp_desc.multisample.mask = ~0u;
    vterm->pipeline = wgpuDeviceCreateRenderPipeline(vterm->device, &rp_desc);
    wgpuPipelineLayoutRelease(layout);
    if (!vterm->pipeline) {
        return YETTY_ERR(yetty_ycore_void, "vterm: render pipeline");
    }

    WGPUSamplerDescriptor sampler_desc = {0};
    sampler_desc.minFilter = WGPUFilterMode_Linear;
    sampler_desc.magFilter = WGPUFilterMode_Linear;
    sampler_desc.addressModeU = WGPUAddressMode_ClampToEdge;
    sampler_desc.addressModeV = WGPUAddressMode_ClampToEdge;
    sampler_desc.maxAnisotropy = 1;
    vterm->sampler = wgpuDeviceCreateSampler(vterm->device, &sampler_desc);
    if (!vterm->sampler) {
        return YETTY_ERR(yetty_ycore_void, "vterm: sampler");
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result vterm_create_cell_buffer(struct yetty_yvterm_vterm *vterm)
{
    uint32_t cols = 0, rows = 0;
    struct yetty_ycore_void_result dims_res =
        yetty_yvterm_grid_dims(vterm->grid_obj, &cols, &rows, NULL);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dims_res, "vterm_create_cell_buffer: grid dims");
    /* The GPU buffer holds ONLY the visible window (rows × cols). The full
     * scrollback ring stays in the CPU model; the GPU can never display rows
     * that aren't on screen, and a single WebGPU storage-buffer binding is
     * capped at 128 MiB — sizing this to the whole ring overflows that cap with
     * deep scrollback (100k lines → ~250 MiB → device-lost). The visible window
     * is re-packed for the current scroll position every frame in
     * vterm_render_grid. */
    uint32_t ring_rows = rows;
    if (vterm->cell_buffer) {
        wgpuBufferDestroy(vterm->cell_buffer);
        wgpuBufferRelease(vterm->cell_buffer);
        vterm->cell_buffer = NULL;
    }
    uint64_t size = (uint64_t)ring_rows * cols * YVTERM_WORDS_PER_CELL * sizeof(uint32_t);
    WGPUBufferDescriptor desc = {0};
    desc.label = vterm_sv("yvterm cells");
    desc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
    desc.size = size;
    vterm->cell_buffer = wgpuDeviceCreateBuffer(vterm->device, &desc);
    if (!vterm->cell_buffer) {
        return YETTY_ERR(yetty_ycore_void, "vterm: cell buffer");
    }
    vterm->cell_buffer_cells = ring_rows * cols;
    vterm->bind_group_valid = 0;

    if (!vterm->uniform_buffer) {
        WGPUBufferDescriptor uni_desc = {0};
        uni_desc.label = vterm_sv("yvterm uniforms");
        uni_desc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        uni_desc.size = sizeof(struct vterm_uniforms);
        vterm->uniform_buffer = wgpuDeviceCreateBuffer(vterm->device, &uni_desc);
        if (!vterm->uniform_buffer) {
            return YETTY_ERR(yetty_ycore_void, "vterm: uniform buffer");
        }
    }
    return YETTY_OK_VOID();
}

/* The font backends publish yrender texture-format enum values; their
 * numbering does NOT match this Dawn header's WGPUTextureFormat, so map
 * explicitly. */
static WGPUTextureFormat vterm_texture_format_to_wgpu(uint32_t yrender_format)
{
    switch (yrender_format) {
    case YETTY_YRENDER_TEXTURE_FORMAT_R8_UNORM:
        return WGPUTextureFormat_R8Unorm;
    case YETTY_YRENDER_TEXTURE_FORMAT_RGBA8_UNORM:
    default:
        return WGPUTextureFormat_RGBA8Unorm;
    }
}

/* Bytes per pixel of the atlas formats the font backends produce. */
static uint32_t vterm_texture_format_bpp(uint32_t yrender_format)
{
    switch (yrender_format) {
    case YETTY_YRENDER_TEXTURE_FORMAT_R8_UNORM:
        return 1u;
    case YETTY_YRENDER_TEXTURE_FORMAT_RGBA8_UNORM:
    default:
        return 4u;
    }
}

static struct yetty_ycore_void_result vterm_recreate_face_atlas(struct yetty_yvterm_vterm *vterm,
                                                                struct yvterm_font_face *face,
                                                                uint32_t width, uint32_t height,
                                                                uint32_t format)
{
    if (face->atlas_view) {
        wgpuTextureViewRelease(face->atlas_view);
        face->atlas_view = NULL;
    }
    if (face->atlas_texture) {
        wgpuTextureDestroy(face->atlas_texture);
        wgpuTextureRelease(face->atlas_texture);
        face->atlas_texture = NULL;
    }
    WGPUTextureDescriptor tex_desc = {0};
    tex_desc.label = vterm_sv("yvterm face atlas");
    tex_desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    tex_desc.dimension = WGPUTextureDimension_2D;
    tex_desc.size.width = width;
    tex_desc.size.height = height;
    tex_desc.size.depthOrArrayLayers = 1;
    tex_desc.format = vterm_texture_format_to_wgpu(format);
    tex_desc.mipLevelCount = 1;
    tex_desc.sampleCount = 1;
    face->atlas_texture = wgpuDeviceCreateTexture(vterm->device, &tex_desc);
    if (!face->atlas_texture) {
        return YETTY_ERR(yetty_ycore_void, "vterm: face atlas texture");
    }
    face->atlas_view = wgpuTextureCreateView(face->atlas_texture, NULL);
    if (!face->atlas_view) {
        return YETTY_ERR(yetty_ycore_void, "vterm: face atlas view");
    }
    face->atlas_width = width;
    face->atlas_height = height;
    face->atlas_format = format;
    face->bytes_per_pixel = vterm_texture_format_bpp(format);
    vterm->bind_group_valid = 0;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result vterm_recreate_face_meta(struct yetty_yvterm_vterm *vterm,
                                                               struct yvterm_font_face *face,
                                                               size_t size)
{
    if (face->meta_buffer) {
        wgpuBufferDestroy(face->meta_buffer);
        wgpuBufferRelease(face->meta_buffer);
        face->meta_buffer = NULL;
    }
    WGPUBufferDescriptor desc = {0};
    desc.label = vterm_sv("yvterm face meta");
    desc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
    desc.size = size;
    face->meta_buffer = wgpuDeviceCreateBuffer(vterm->device, &desc);
    if (!face->meta_buffer) {
        return YETTY_ERR(yetty_ycore_void, "vterm: face meta buffer");
    }
    face->meta_capacity = size;
    vterm->bind_group_valid = 0;
    return YETTY_OK_VOID();
}

/* Upload one face's atlas + glyph metadata to its GPU copies, and refresh the
 * per-face geometry uniforms. Face 0 additionally feeds the legacy scalar
 * uniforms (scale/baseline drive underline placement and figure zoom). */
static struct yetty_ycore_void_result vterm_upload_face(struct yetty_yvterm_vterm *vterm,
                                                        uint32_t face_index)
{
    struct yvterm_font_face *face = &vterm->faces[face_index];
    struct yetty_yrender_gpu_resource_set_result rs_res =
        face->font->ops->get_gpu_resource_set(face->font);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rs_res, "vterm_upload_face: get_gpu_resource_set");
    const struct yetty_yrender_gpu_resource_set *rs = rs_res.value;

    /* MSDF faces publish (pixel_range, scale, baseline_y, glyph_left); raster
     * faces pre-rasterize at cell size and need no geometry uniforms. */
    if (face->method == YVTERM_FONT_METHOD_MSDF) {
        vterm->uniforms.face_params[face_index][0] = rs->uniforms[0].f32;
        vterm->uniforms.face_params[face_index][1] = rs->uniforms[1].f32;
        vterm->uniforms.face_params[face_index][2] = rs->uniforms[2].f32;
        vterm->uniforms.face_params[face_index][3] = rs->uniforms[3].f32;
        if (face_index == 0) {
            vterm->uniforms.pixel_range = rs->uniforms[0].f32;
            vterm->uniforms.scale = rs->uniforms[1].f32;
            vterm->uniforms.baseline_y = rs->uniforms[2].f32;
            vterm->uniforms.glyph_left = rs->uniforms[3].f32;
        }
    }

    const struct yetty_yrender_texture *atlas = &rs->textures[0];
    if (atlas->data && atlas->width > 0 && atlas->height > 0) {
        uint32_t format =
            atlas->format ? atlas->format : (uint32_t)YETTY_YRENDER_TEXTURE_FORMAT_RGBA8_UNORM;
        if (!face->atlas_texture || atlas->width != face->atlas_width ||
            atlas->height != face->atlas_height || format != face->atlas_format) {
            struct yetty_ycore_void_result re =
                vterm_recreate_face_atlas(vterm, face, atlas->width, atlas->height, format);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, re, "vterm_upload_face: recreate_atlas");
        }
        WGPUTexelCopyTextureInfo dest = {0};
        dest.texture = face->atlas_texture;
        dest.mipLevel = 0;
        WGPUTexelCopyBufferLayout src_layout = {0};
        src_layout.bytesPerRow = atlas->width * face->bytes_per_pixel;
        src_layout.rowsPerImage = atlas->height;
        WGPUExtent3D extent = {atlas->width, atlas->height, 1};
        size_t bytes = (size_t)atlas->width * atlas->height * face->bytes_per_pixel;
        wgpuQueueWriteTexture(vterm->queue, &dest, atlas->data, bytes, &src_layout, &extent);
    }

    const struct yetty_yrender_buffer *meta = &rs->buffers[0];
    if (meta->data && meta->size > 0) {
        if (!face->meta_buffer || meta->size > face->meta_capacity) {
            struct yetty_ycore_void_result re = vterm_recreate_face_meta(vterm, face, meta->size);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, re, "vterm_upload_face: recreate_meta");
        }
        wgpuQueueWriteBuffer(vterm->queue, face->meta_buffer, 0, meta->data, meta->size);
    }
    return YETTY_OK_VOID();
}

/* Upload every dirty face (face 0 = base font is always present). */
static struct yetty_ycore_void_result vterm_upload_dirty_faces(struct yetty_yvterm_vterm *vterm)
{
    for (uint32_t i = 0; i < vterm->face_count; ++i) {
        struct yetty_yfont_ms_font *font = vterm->faces[i].font;
        if (!font) {
            continue;
        }
        if (!vterm->faces[i].atlas_texture || font->ops->is_dirty(font)) {
            struct yetty_ycore_void_result up = vterm_upload_face(vterm, i);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, up, "vterm_upload_dirty_faces");
        }
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result vterm_ensure_bind_group(struct yetty_yvterm_vterm *vterm)
{
    if (vterm->bind_group_valid && vterm->bind_group) {
        return YETTY_OK_VOID();
    }
    if (vterm->bind_group) {
        wgpuBindGroupRelease(vterm->bind_group);
        vterm->bind_group = NULL;
    }
    struct yvterm_font_face *base = &vterm->faces[0];
    if (!vterm->cell_buffer || !base->meta_buffer || !base->atlas_view || !vterm->sampler ||
        !vterm->uniform_buffer) {
        return YETTY_ERR(yetty_ycore_void, "vterm_ensure_bind_group: missing resource");
    }
    WGPUBindGroupEntry entries[11] = {0};
    entries[0].binding = 0;
    entries[0].buffer = vterm->cell_buffer;
    entries[0].size = WGPU_WHOLE_SIZE;
    entries[1].binding = 1;
    entries[1].buffer = base->meta_buffer;
    entries[1].size = WGPU_WHOLE_SIZE;
    entries[2].binding = 2;
    entries[2].textureView = base->atlas_view;
    entries[3].binding = 3;
    entries[3].sampler = vterm->sampler;
    entries[4].binding = 4;
    entries[4].buffer = vterm->uniform_buffer;
    entries[4].size = sizeof(struct vterm_uniforms);
    /* Extra face slots; unused ones alias the base resources (the shader
     * never reads them for face 0 cells, but the layout needs a binding). */
    for (uint32_t i = 1; i < YVTERM_MAX_FONT_FACES; ++i) {
        struct yvterm_font_face *face = &vterm->faces[i];
        int usable = face->font && face->meta_buffer && face->atlas_view;
        entries[3 + i * 2].binding = 3 + i * 2; /* 5, 7, 9 */
        entries[3 + i * 2].buffer = usable ? face->meta_buffer : base->meta_buffer;
        entries[3 + i * 2].size = WGPU_WHOLE_SIZE;
        entries[4 + i * 2].binding = 4 + i * 2; /* 6, 8, 10 */
        entries[4 + i * 2].textureView = usable ? face->atlas_view : base->atlas_view;
    }
    WGPUBindGroupDescriptor bg_desc = {0};
    bg_desc.layout = vterm->bind_group_layout;
    bg_desc.entryCount = 11;
    bg_desc.entries = entries;
    vterm->bind_group = wgpuDeviceCreateBindGroup(vterm->device, &bg_desc);
    if (!vterm->bind_group) {
        return YETTY_ERR(yetty_ycore_void, "vterm_ensure_bind_group: create");
    }
    vterm->bind_group_valid = 1;
    return YETTY_OK_VOID();
}

/* Cell padding around the glyph from config, as fractions of the glyph
 * dimensions (terminal/text-layer/font/padding/{left,right,top,bottom}).
 * Negative values tighten the cell below the glyph extent so contour glyphs
 * (box drawing) overlap their neighbours and connect without seams. */
static struct yetty_yfont_ms_padding vterm_font_padding_from_config(
    const struct yetty_yconfig_config *config)
{
    struct yetty_yfont_ms_padding padding = {0};
    if (!config) {
        return padding;
    }
    padding.left = strtof(
        config->ops->get_string(config, "terminal/text-layer/font/padding/left", "0.0"), NULL);
    padding.right = strtof(
        config->ops->get_string(config, "terminal/text-layer/font/padding/right", "0.0"), NULL);
    padding.top = strtof(
        config->ops->get_string(config, "terminal/text-layer/font/padding/top", "0.0"), NULL);
    padding.bottom = strtof(
        config->ops->get_string(config, "terminal/text-layer/font/padding/bottom", "0.0"), NULL);
    return padding;
}

/* Resolve the MSDF CDB path for a font `name` (installed -> cache -> GPU
 * generation), delegating to the shared yfont resolver so the base face, the
 * range faces, and the non-terminal font consumers all share one policy and
 * one set of log messages. */
static struct yetty_ycore_void_result vterm_resolve_msdf_cdb(
    struct yetty_yvterm_vterm *vterm, struct yetty_yconfig_config *config, const char *name,
    const char *fonts_dir, char *cdb_path_out, size_t cdb_path_cap)
{
    const char *cache_dir = config ? config->ops->get_string(config, "paths/cache", "") : "";
    struct yetty_ymsdf_generator *generator =
        vterm->runtime ? vterm->runtime->gpu.msdf_generator : NULL;
    return yetty_yfont_msdf_resolve_cdb(generator, fonts_dir, cache_dir, name, "-Regular",
                                        cdb_path_out, cdb_path_cap);
}

/* Find an existing face for (name, method) or create it. Returns the face
 * index, or 0 (the base font) when the face can't be created or the face
 * table is full — the range then simply renders with the base font. */
static uint32_t vterm_face_find_or_create(struct yetty_yvterm_vterm *vterm,
                                          struct yetty_yconfig_config *config, const char *name,
                                          enum yvterm_font_method method, const char *fonts_dir,
                                          const char *shaders_dir, float font_size)
{
    for (uint32_t i = 0; i < vterm->face_count; ++i) {
        enum yvterm_font_method existing = vterm->faces[i].method;
        int method_matches = existing == method || (method == YVTERM_FONT_METHOD_RASTER &&
                                                    existing == YVTERM_FONT_METHOD_RASTER_COLOR);
        if (method_matches && strcmp(vterm->faces[i].name, name) == 0) {
            return i;
        }
    }
    if (vterm->face_count >= YVTERM_MAX_FONT_FACES) {
        ywarn("vterm: font face table full (%u), range font '%s' falls back to the base font",
              (unsigned)YVTERM_MAX_FONT_FACES, name);
        return 0;
    }

    struct yetty_font_ms_font_result font_res;
    if (method == YVTERM_FONT_METHOD_RASTER) {
        struct pixel_size_result cell =
            vterm->faces[0].font->ops->get_cell_size(vterm->faces[0].font);
        if (YETTY_IS_ERR(cell)) {
            yetty_ycore_error_destroy(cell.error);
            return 0;
        }
        font_res = yetty_yfont_ms_raster_font_create_named(config, name, cell.value.width,
                                                           cell.value.height);
    } else {
        /* Prebaked CDB next to the bundled fonts wins; otherwise the shared
         * resolver generates one on the GPU from the face's TTF into the cache
         * dir (same policy as the base face). */
        char cdb_path[768];
        char font_shader_path[768];
        struct yetty_ycore_void_result cdb_res =
            vterm_resolve_msdf_cdb(vterm, config, name, fonts_dir, cdb_path, sizeof(cdb_path));
        if (YETTY_IS_ERR(cdb_res)) {
            ywarn("vterm: range font '%s' has no usable CDB (%s) — falling back to the base font",
                  name, cdb_res.error.msg);
            yetty_ycore_error_destroy(cdb_res.error);
            return 0;
        }
        snprintf(font_shader_path, sizeof(font_shader_path), "%s/ms-msdf-font.wgsl", shaders_dir);
        /* Same padding as the base face — range faces must share the cell
         * geometry the base font establishes. */
        struct yetty_yfont_ms_padding padding = vterm_font_padding_from_config(config);
        font_res = yetty_yfont_ms_msdf_font_create(cdb_path, font_shader_path, font_size, padding);
    }
    if (YETTY_IS_ERR(font_res)) {
        ywarn("vterm: range font '%s' (%s) failed, falling back to the base font: %s", name,
              method == YVTERM_FONT_METHOD_RASTER ? "raster" : "msdf", font_res.error.msg);
        yetty_ycore_error_destroy(font_res.error);
        return 0;
    }

    uint32_t index = vterm->face_count++;
    struct yvterm_font_face *face = &vterm->faces[index];
    face->font = font_res.value;
    face->method = method;
    strncpy(face->name, name, YVTERM_FONT_FACE_NAME_MAX - 1);

    /* A raster face with an RGBA8 atlas is a color (emoji) face. */
    if (method == YVTERM_FONT_METHOD_RASTER) {
        struct yetty_yrender_gpu_resource_set_result rs_res =
            face->font->ops->get_gpu_resource_set(face->font);
        if (YETTY_IS_OK(rs_res) &&
            rs_res.value->textures[0].format == YETTY_YRENDER_TEXTURE_FORMAT_RGBA8_UNORM) {
            face->method = YVTERM_FONT_METHOD_RASTER_COLOR;
        } else if (YETTY_IS_ERR(rs_res)) {
            yetty_ycore_error_destroy(rs_res.error);
        }
    }
    return index;
}

/* Build the codepoint-range → face routing table from the config list
 * (terminal/text-layer/font/ranges: {from, to, font, render-method}). */
static void vterm_load_font_ranges(struct yetty_yvterm_vterm *vterm,
                                   struct yetty_yconfig_config *config, const char *fonts_dir,
                                   const char *shaders_dir, float font_size)
{
    int count = config->ops->get_array_count(config, YETTY_YCONFIG_KEY_TERMINAL_FONT_RANGES);
    for (int i = 0; i < count && vterm->font_range_count < YVTERM_MAX_FONT_RANGES; ++i) {
        char path[128];
        snprintf(path, sizeof(path), "%s/%d/from", YETTY_YCONFIG_KEY_TERMINAL_FONT_RANGES, i);
        const char *from_text = config->ops->get_string(config, path, NULL);
        snprintf(path, sizeof(path), "%s/%d/to", YETTY_YCONFIG_KEY_TERMINAL_FONT_RANGES, i);
        const char *to_text = config->ops->get_string(config, path, NULL);
        snprintf(path, sizeof(path), "%s/%d/font", YETTY_YCONFIG_KEY_TERMINAL_FONT_RANGES, i);
        const char *font_name = config->ops->get_string(config, path, NULL);
        snprintf(path, sizeof(path), "%s/%d/render-method", YETTY_YCONFIG_KEY_TERMINAL_FONT_RANGES,
                 i);
        const char *method_text = config->ops->get_string(config, path, "msdf");
        if (!from_text || !to_text || !font_name || !font_name[0]) {
            ywarn("vterm: font range %d is missing from/to/font — skipped", i);
            continue;
        }
        uint32_t from_codepoint = (uint32_t)strtoul(from_text, NULL, 0);
        uint32_t to_codepoint = (uint32_t)strtoul(to_text, NULL, 0);
        if (to_codepoint < from_codepoint) {
            ywarn("vterm: font range %d has to < from (%s..%s) — skipped", i, from_text, to_text);
            continue;
        }
        enum yvterm_font_method method = strcmp(method_text, "raster") == 0
                                             ? YVTERM_FONT_METHOD_RASTER
                                             : YVTERM_FONT_METHOD_MSDF;
        uint32_t face = vterm_face_find_or_create(vterm, config, font_name, method, fonts_dir,
                                                  shaders_dir, font_size);
        if (face == 0) {
            continue; /* base font already covers unrouted codepoints */
        }
        struct yvterm_font_range *range = &vterm->font_ranges[vterm->font_range_count++];
        range->from = from_codepoint;
        range->to = to_codepoint;
        range->face = face;
    }

    /* Publish per-face render methods to the shader (4 bits per face). */
    uint32_t methods = 0;
    for (uint32_t i = 0; i < vterm->face_count; ++i) {
        methods |= ((uint32_t)vterm->faces[i].method & 0xFu) << (i * 4u);
    }
    vterm->uniforms.face_methods = methods;
}

/* Best-effort GPU setup. On a soft failure (missing device, font/pipeline/SDF
 * init failure) it leaves gpu_ready=0 and returns OK so the figure still exists
 * (blank pane) rather than failing terminal creation — the inner errors are
 * logged and consumed here by design, not propagated. */
static struct yetty_ycore_void_result vterm_gpu_init(struct yetty_yvterm_vterm *vterm,
                                                     const struct yetty_context *context)
{
    if (!context || !context->runtime) {
        return YETTY_OK_VOID();
    }
    struct yetty_yframework *runtime = context->runtime;
    vterm->runtime = runtime;
    vterm->device = runtime->gpu.device;
    vterm->queue = runtime->gpu.queue;
    vterm->target_format = runtime->gpu.surface_format;
    if (!vterm->device || !vterm->queue) {
        return YETTY_OK_VOID();
    }

    struct yetty_yconfig_config *config = runtime->config;
    const char *fonts_dir = config ? config->ops->get_string(config, "paths/fonts", "") : "";
    const char *shaders_dir = config ? config->ops->get_string(config, "paths/shaders", "") : "";
    float content_scale = runtime->gpu.app_gpu_context.content_scale;
    if (content_scale <= 0.0f) {
        content_scale = 1.0f;
    }
    /* Config carries the font size in logical (CSS-style) pixels; scale once
     * here to framebuffer pixels so the glyphs render at the right physical
     * size on HiDPI displays without every other pipeline stage having to know
     * about content_scale. */
    int cfg_size = config ? config->ops->get_int(config, "terminal/text-layer/font/size", 14) : 14;
    float font_size = (float)cfg_size * content_scale;

    /* Base font settings from config: family (font/family), rendering method
     * (terminal/text-layer/font/render-method) and cell padding. */
    const char *font_family = config ? config->ops->font_family(config) : NULL;
    if (!font_family || !font_family[0] || strcmp(font_family, "default") == 0) {
        font_family = "DejaVuSansMNerdFontMono";
    }
    const char *render_method =
        config
            ? config->ops->get_string(config, YETTY_YCONFIG_KEY_TERMINAL_FONT_RENDER_METHOD, "msdf")
            : "msdf";
    struct yetty_yfont_ms_padding padding = vterm_font_padding_from_config(config);

    struct yetty_font_ms_font_result font_res;
    enum yvterm_font_method base_method;
    if (config && strcmp(render_method, "raster") == 0) {
        /* FreeType-rasterized base face (reads font/family itself). The cell
         * is derived from the configured size: glyph line box fits the cell
         * height, monospace 1:2 aspect for the width, padding applied the
         * same way the MSDF cell derivation does. */
        float cell_height = font_size * (1.0f + padding.top + padding.bottom);
        float cell_width = font_size * 0.5f * (1.0f + padding.left + padding.right);
        font_res = yetty_yfont_ms_raster_font_create(config, cell_width, cell_height);
        base_method = YVTERM_FONT_METHOD_RASTER;
    } else {
        char cdb_path[768];
        char font_shader_path[768];
        /* Prebaked CDB wins; otherwise generate it on the GPU from the family's
         * TTF, so a configured font that ships no baked atlas still renders
         * rather than failing base-font creation. */
        struct yetty_ycore_void_result cdb_res = vterm_resolve_msdf_cdb(
            vterm, config, font_family, fonts_dir, cdb_path, sizeof(cdb_path));
        if (YETTY_IS_ERR(cdb_res)) {
            ywarn("vterm: base font '%s' has no usable CDB: %s", font_family, cdb_res.error.msg);
            yetty_ycore_error_destroy(cdb_res.error);
            return YETTY_OK_VOID();
        }
        snprintf(font_shader_path, sizeof(font_shader_path), "%s/ms-msdf-font.wgsl", shaders_dir);
        font_res = yetty_yfont_ms_msdf_font_create(cdb_path, font_shader_path, font_size, padding);
        base_method = YVTERM_FONT_METHOD_MSDF;
    }
    if (YETTY_IS_ERR(font_res)) {
        ywarn("vterm: base font '%s' (%s) create failed: %s", font_family, render_method,
              font_res.error.msg);
        yetty_ycore_error_destroy(font_res.error);
        return YETTY_OK_VOID();
    }
    struct yvterm_font_face *base_face = &vterm->faces[0];
    base_face->font = font_res.value;
    base_face->method = base_method;
    /* The family name (not a placeholder) so a config range naming the same
     * font resolves to this face instead of loading a duplicate. */
    strncpy(base_face->name, font_family, YVTERM_FONT_FACE_NAME_MAX - 1);
    vterm->face_count = 1;
    struct yetty_ycore_void_result latin = base_face->font->ops->load_basic_latin(base_face->font);
    if (YETTY_IS_ERR(latin)) {
        yetty_ycore_error_destroy(latin.error);
    }
    struct pixel_size_result cell = base_face->font->ops->get_cell_size(base_face->font);
    if (YETTY_IS_OK(cell)) {
        vterm->cell_size = cell.value;
    }

    /* Config codepoint-range faces (CJK/emoji/etc.) — created after the base
     * font so raster faces inherit its cell size. Best-effort: a face that
     * fails to load just routes its ranges back to the base font. */
    if (config) {
        vterm_load_font_ranges(vterm, config, fonts_dir, shaders_dir, font_size);
    }

    /* Load the effects library so vterm_create_pipeline can prepend it to the
     * text shader. Non-fatal: a stub is used if the asset is absent (effects
     * become no-ops but text still renders). */
    if (shaders_dir && shaders_dir[0]) {
        char fx_path[512];
        snprintf(fx_path, sizeof(fx_path), "%s/effects-lib.wgsl", shaders_dir);
        struct yetty_ycore_buffer_result fx = yetty_ycore_read_file(fx_path);
        if (YETTY_IS_OK(fx)) {
            vterm->effects_lib_code = fx.value;
        } else {
            ywarn("vterm: effects-lib.wgsl not loaded (%s) — effects disabled",
                  fx.error.msg ? fx.error.msg : "read failed");
            yetty_ycore_error_destroy(fx.error);
        }
    }

    struct yetty_ycore_void_result pr = vterm_create_pipeline(vterm);
    if (YETTY_IS_ERR(pr)) {
        ywarn("vterm: pipeline init failed: %s", pr.error.msg);
        yetty_ycore_error_destroy(pr.error);
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result cbr = vterm_create_cell_buffer(vterm);
    if (YETTY_IS_ERR(cbr)) {
        yetty_ycore_error_destroy(cbr.error);
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result ufr = vterm_upload_dirty_faces(vterm);
    if (YETTY_IS_ERR(ufr)) {
        yetty_ycore_error_destroy(ufr.error);
        return YETTY_OK_VOID();
    }
    vterm->uniforms.cell_size[0] = vterm->cell_size.width;
    vterm->uniforms.cell_size[1] = vterm->cell_size.height;

    /* SDF/glyph/text renderer for raw ydraw records (ycat). Best-effort: a
     * failure here leaves sdf_layer NULL and the rich SDF pass is skipped, but
     * text + composites still render. */
    struct yetty_yvterm_sdf_layer_ptr_result sdf_res = yetty_yvterm_sdf_layer_create(context);
    if (YETTY_IS_OK(sdf_res)) {
        vterm->sdf_layer = sdf_res.value;
    } else {
        ywarn("vterm: SDF layer init failed (ycat rich content disabled): %s", sdf_res.error.msg);
        yetty_ycore_error_destroy(sdf_res.error);
    }

    /* Animated shader-glyph renderer for PUA-B cells. The anim timer needs this
     * figure object to mark itself dirty; recover it from the body. Best-effort:
     * a failure leaves it NULL and shader glyphs simply don't animate. */
    struct yetty_yclass_object_ptr_result self_res = yetty_yvterm_vterm_to(vterm);
    struct yetty_yclass_object *self_obj = YETTY_IS_OK(self_res) ? self_res.value : NULL;
    if (YETTY_IS_ERR(self_res)) {
        yetty_ycore_error_destroy(self_res.error);
    }
    struct yetty_yvterm_shader_glyph_layer_ptr_result sg_res =
        yetty_yvterm_shader_glyph_layer_create(context, self_obj, vterm->sink);
    if (YETTY_IS_OK(sg_res)) {
        vterm->shader_glyph_layer = sg_res.value;
    } else {
        ywarn("vterm: shader-glyph layer init failed (animated glyphs disabled): %s",
              sg_res.error.msg);
        yetty_ycore_error_destroy(sg_res.error);
    }

    vterm->gpu_ready = 1;
    return YETTY_OK_VOID();
}

static void vterm_gpu_destroy(struct yetty_yvterm_vterm *vterm)
{
    if (vterm->shader_glyph_layer) {
        yetty_yvterm_shader_glyph_layer_destroy(vterm->shader_glyph_layer);
        vterm->shader_glyph_layer = NULL;
    }
    if (vterm->sdf_layer) {
        yetty_yvterm_sdf_layer_destroy(vterm->sdf_layer);
        vterm->sdf_layer = NULL;
    }
    if (vterm->bind_group) {
        wgpuBindGroupRelease(vterm->bind_group);
        vterm->bind_group = NULL;
    }
    for (uint32_t i = 0; i < YVTERM_MAX_FONT_FACES; ++i) {
        struct yvterm_font_face *face = &vterm->faces[i];
        if (face->atlas_view) {
            wgpuTextureViewRelease(face->atlas_view);
            face->atlas_view = NULL;
        }
        if (face->atlas_texture) {
            wgpuTextureDestroy(face->atlas_texture);
            wgpuTextureRelease(face->atlas_texture);
            face->atlas_texture = NULL;
        }
        if (face->meta_buffer) {
            wgpuBufferDestroy(face->meta_buffer);
            wgpuBufferRelease(face->meta_buffer);
            face->meta_buffer = NULL;
        }
    }
    if (vterm->uniform_buffer) {
        wgpuBufferDestroy(vterm->uniform_buffer);
        wgpuBufferRelease(vterm->uniform_buffer);
        vterm->uniform_buffer = NULL;
    }
    if (vterm->cell_buffer) {
        wgpuBufferDestroy(vterm->cell_buffer);
        wgpuBufferRelease(vterm->cell_buffer);
        vterm->cell_buffer = NULL;
    }
    if (vterm->sampler) {
        wgpuSamplerRelease(vterm->sampler);
        vterm->sampler = NULL;
    }
    if (vterm->pipeline) {
        wgpuRenderPipelineRelease(vterm->pipeline);
        vterm->pipeline = NULL;
    }
    if (vterm->bind_group_layout) {
        wgpuBindGroupLayoutRelease(vterm->bind_group_layout);
        vterm->bind_group_layout = NULL;
    }
    if (vterm->shader_module) {
        wgpuShaderModuleRelease(vterm->shader_module);
        vterm->shader_module = NULL;
    }
    free(vterm->combined_shader);
    vterm->combined_shader = NULL;
    free(vterm->effects_lib_code.data);
    vterm->effects_lib_code.data = NULL;
    free(vterm->row_scratch);
    vterm->row_scratch = NULL;
    vterm->row_scratch_cols = 0;
    for (uint32_t i = 0; i < YVTERM_MAX_FONT_FACES; ++i) {
        if (vterm->faces[i].font) {
            vterm->faces[i].font->ops->destroy(vterm->faces[i].font);
            vterm->faces[i].font = NULL;
        }
    }
    vterm->face_count = 0;
    vterm->font_range_count = 0;
    vterm->gpu_ready = 0;
}

/* Walk dirty model lines, upload them, and draw the grid into the target. */
static struct yetty_ycore_void_result vterm_render_grid(struct yetty_yvterm_vterm *vterm,
                                                        struct yetty_ydraw_target *target,
                                                        struct yetty_ycore_rectangle rect)
{
    if (!vterm->gpu_ready || !target || !target->ops || !target->ops->get_view) {
        return YETTY_OK_VOID();
    }
    struct yetty_yclass_object *grid = vterm->grid_obj;
    uint32_t cols = 0, rows = 0;
    struct yetty_ycore_void_result dims_res = yetty_yvterm_grid_dims(grid, &cols, &rows, NULL);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dims_res, "vterm_render: grid dims");
    if (cols == 0 || rows == 0) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_uint32_result slot_count_res = yetty_yvterm_grid_slot_count(grid);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, slot_count_res, "vterm_render: slot count");
    uint32_t slot_count = slot_count_res.value;
    /* The GPU buffer holds only the visible window, so it tracks rows × cols —
     * resize on a grid geometry change, not on scrollback depth. */
    if (vterm->cell_buffer_cells != rows * cols) {
        struct yetty_ycore_void_result br = vterm_create_cell_buffer(vterm);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, br, "vterm_render: resize cell buffer");
    }
    struct yetty_ycore_void_result rsr = vterm_ensure_row_scratch(vterm, cols);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rsr, "vterm_render: row scratch");

    /* Resolve the frame's view window through the tiered scroll buffer: one
     * slot id per window row (visible rows + the bottom-anchor look-ahead).
     * Hot rows come back as real ring slots; archived rows come back as
     * extended ids served from the grid's materialization cache — the slot
     * accessors below read both uniformly, and the grid has already
     * re-materialized figures, swept stale archive runtimes, and prefetched
     * the adjacent segment. `back` (how deep the view sits behind the live
     * top) caps the look-ahead exactly as before. */
    uint32_t back = 0;
    int view_active = 0;
    uint64_t view_top = 0;
    struct yetty_ycore_void_result view_res = yetty_yvterm_grid_view(grid, &view_active, &view_top);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, view_res, "vterm_render: grid view");
    if (view_active) {
        struct yetty_ycore_uint64_result live_anchor_res = yetty_yvterm_grid_live_anchor(grid);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, live_anchor_res, "vterm_render: live anchor");
        if (view_top < live_anchor_res.value) {
            uint64_t distance = live_anchor_res.value - view_top;
            back = distance > UINT32_MAX ? UINT32_MAX : (uint32_t)distance;
        }
    }
    int comp_lookahead = (int)(back < (uint32_t)YVTERM_COMPOSITE_ANCHOR_LOOKAHEAD_ROWS
                                   ? back
                                   : (uint32_t)YVTERM_COMPOSITE_ANCHOR_LOOKAHEAD_ROWS);
    uint32_t window_rows = 0;
    struct yetty_ycore_const_uint32_ptr_result window_res =
        yetty_yvterm_grid_view_window(grid, rows + (uint32_t)comp_lookahead, &window_rows);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, window_res, "vterm_render: view window");
    const uint32_t *window_slots = window_res.value;
    if (!window_slots || window_rows == 0) {
        return YETTY_OK_VOID();
    }

    /* Pack ONLY the visible window into the GPU buffer, indexed by visible row
     * [0, rows). The text shader reads cell_index = row*cols + col with
     * root_row 0 (set below) — no off-screen scrollback resident on the GPU.
     * The window is small (rows × cols), so re-packing it each frame is cheap
     * and glyph resolution stays cached in the font. */
    size_t row_bytes = (size_t)cols * YVTERM_WORDS_PER_CELL * sizeof(uint32_t);
    for (uint32_t r = 0; r < rows && r < window_rows; ++r) {
        struct yetty_yvterm_text_cell_const_ptr_result cells_res =
            yetty_yvterm_grid_slot_cells(grid, window_slots[r]);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, cells_res, "vterm_render: slot cells");
        const struct yetty_yvterm_text_cell *cells = cells_res.value;
        if (!cells) {
            continue;
        }
        vterm_pack_line(vterm, cells, cols, vterm->row_scratch);
        wgpuQueueWriteBuffer(vterm->queue, vterm->cell_buffer, (uint64_t)r * row_bytes,
                             vterm->row_scratch, row_bytes);
    }
    /* Glyph resolution above may have grown an atlas/meta — re-pull dirty faces. */
    struct yetty_ycore_void_result faces_res = vterm_upload_dirty_faces(vterm);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, faces_res, "vterm_render: upload faces");

    uint32_t cursor_row = 0, cursor_col = 0, cursor_visible = 0;
    struct yetty_ycore_void_result cursor_res =
        yetty_yvterm_grid_cursor(grid, &cursor_row, &cursor_col, &cursor_visible);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, cursor_res, "vterm_render: grid cursor");
    vterm->uniforms.grid_size[0] = (float)cols;
    vterm->uniforms.grid_size[1] = (float)rows;
    vterm->uniforms.cell_size[0] = vterm->cell_size.width;
    vterm->uniforms.cell_size[1] = vterm->cell_size.height;
    /* The GPU buffer is the visible window indexed by visible row, so the text
     * shader maps row→cell with root_row 0 over `rows` ring rows. (The real
     * ring root_row, computed above, still drives the model-reading passes.) */
    vterm->uniforms.ring_rows = rows;
    vterm->uniforms.root_row = 0u;
    vterm->uniforms.cursor_col = cursor_col;
    vterm->uniforms.cursor_row = cursor_row;
    /* Hide the cursor while scrolled back — it belongs to the live bottom, not
     * the historical rows on screen. */
    vterm->uniforms.cursor_visible = view_active ? 0u : cursor_visible;
    /* Selection highlight: normalise (anchor, head) to reading order so the
     * shader's start<=end stream test is simple. */
    int sel_active = 0;
    uint32_t sa_row = 0, sa_col = 0, sh_row = 0, sh_col = 0;
    struct yetty_ycore_void_result sel_res =
        yetty_yvterm_grid_selection(grid, &sel_active, &sa_row, &sa_col, &sh_row, &sh_col);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sel_res, "vterm_render: grid selection");
    if (sel_active && (sa_row > sh_row || (sa_row == sh_row && sa_col > sh_col))) {
        uint32_t tr = sa_row, tc = sa_col;
        sa_row = sh_row;
        sa_col = sh_col;
        sh_row = tr;
        sh_col = tc;
    }
    vterm->uniforms.sel_active = (sel_active && !view_active) ? 1u : 0u;
    vterm->uniforms.sel_start_row = sa_row;
    vterm->uniforms.sel_start_col = sa_col;
    vterm->uniforms.sel_end_row = sh_row;
    vterm->uniforms.sel_end_col = sh_col;
    /* Same visual-zoom transform the composite pass uses, so the text plane and
     * anchored figures magnify and pan together (keeps a figure locked to its
     * row under Ctrl+wheel zoom). */
    vterm->uniforms.visual_zoom_scale =
        vterm->visual_zoom_scale > 0.0f ? vterm->visual_zoom_scale : 1.0f;
    vterm->uniforms.visual_zoom_offset_x = vterm->visual_zoom_offset_x;
    vterm->uniforms.visual_zoom_offset_y = vterm->visual_zoom_offset_y;
    /* Shared animation clock + OSC-driven post-color effect selection. Every
     * shader this frame reads the same frame_time_sec, so effects stay in
     * phase across the text plane, ydraw layer and figures. */
    vterm->uniforms.time = vterm->runtime ? (float)vterm->runtime->frame_time_sec : 0.0f;
    vterm->uniforms.post_fx_index = vterm->post_fx_index;
    vterm->uniforms.post_fx_p0 = vterm->post_fx_params[0];
    vterm->uniforms.post_fx_p1 = vterm->post_fx_params[1];
    vterm->uniforms.post_fx_p2 = vterm->post_fx_params[2];
    vterm->uniforms.post_fx_p3 = vterm->post_fx_params[3];
    vterm->uniforms.post_fx_p4 = vterm->post_fx_params[4];
    vterm->uniforms.post_fx_p5 = vterm->post_fx_params[5];
    vterm->uniforms.coord_fx_index = vterm->coord_fx_index;
    vterm->uniforms.coord_fx_p0 = vterm->coord_fx_params[0];
    vterm->uniforms.coord_fx_p1 = vterm->coord_fx_params[1];
    vterm->uniforms.coord_fx_p2 = vterm->coord_fx_params[2];
    vterm->uniforms.coord_fx_p3 = vterm->coord_fx_params[3];
    vterm->uniforms.coord_fx_p4 = vterm->coord_fx_params[4];
    vterm->uniforms.coord_fx_p5 = vterm->coord_fx_params[5];
    /* Live pointer, or the cursor cell centre until the mouse first enters.
     * mouse_local_* is pane-local; the shader works in content-rect space, so
     * shift by the content origin (0,0 when the content fills the pane). */
    if (vterm->have_mouse) {
        vterm->uniforms.mouse_x = vterm->mouse_local_x - vterm->content_rect_x;
        vterm->uniforms.mouse_y = vterm->mouse_local_y - vterm->content_rect_y;
    } else {
        vterm->uniforms.mouse_x = ((float)cursor_col + 0.5f) * vterm->cell_size.width;
        vterm->uniforms.mouse_y = ((float)cursor_row + 0.5f) * vterm->cell_size.height;
    }
    wgpuQueueWriteBuffer(vterm->queue, vterm->uniform_buffer, 0, &vterm->uniforms,
                         sizeof(struct vterm_uniforms));

    struct yetty_ycore_void_result bg = vterm_ensure_bind_group(vterm);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, bg, "vterm_render: bind group");

    WGPUTextureView view = target->ops->get_view(target);
    if (!view) {
        return YETTY_ERR(yetty_ycore_void, "vterm_render: target view NULL");
    }
    /* The figure rect is pane-local (origin 0,0); the pane's framebuffer
     * position lives in the target viewport origin (e.g. y=36 below the
     * tabbar). Offset the draw viewport by it so terminal row 0 lands at the
     * pane top instead of behind the tabbar — otherwise an idle shell's single
     * prompt row is drawn at y=0 and scissored away, leaving a blank pane. */
    float vx = target->viewport.x + rect.min.x;
    float vy = target->viewport.y + rect.min.y;
    float w = rect.max.x - rect.min.x;
    float h = rect.max.y - rect.min.y;
    if (w <= 0.0f || h <= 0.0f) {
        return YETTY_OK_VOID();
    }

    WGPUCommandEncoderDescriptor enc_desc = {0};
    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(vterm->device, &enc_desc);
    if (!enc) {
        return YETTY_ERR(yetty_ycore_void, "vterm_render: encoder");
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
    if (!pass) {
        wgpuCommandEncoderRelease(enc);
        return YETTY_ERR(yetty_ycore_void, "vterm_render: begin pass");
    }
    wgpuRenderPassEncoderSetViewport(pass, vx, vy, w, h, 0.0f, 1.0f);
    /* Clamp scissor to the target viewport. */
    float tx0 = target->viewport.x, ty0 = target->viewport.y;
    float tx1 = tx0 + target->viewport.w, ty1 = ty0 + target->viewport.h;
    float sx0 = vx > tx0 ? vx : tx0;
    float sy0 = vy > ty0 ? vy : ty0;
    float sx1 = (vx + w) < tx1 ? (vx + w) : tx1;
    float sy1 = (vy + h) < ty1 ? (vy + h) : ty1;
    if (sx1 > sx0 && sy1 > sy0) {
        wgpuRenderPassEncoderSetScissorRect(pass, (uint32_t)sx0, (uint32_t)sy0,
                                            (uint32_t)(sx1 - sx0), (uint32_t)(sy1 - sy0));
        wgpuRenderPassEncoderSetPipeline(pass, vterm->pipeline);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, vterm->bind_group, 0, NULL);
        wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);
    }
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    WGPUCommandBufferDescriptor cmd_desc = {0};
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(enc, &cmd_desc);
    wgpuQueueSubmit(vterm->queue, 1, &cmd);
    wgpuCommandBufferRelease(cmd);
    wgpuCommandEncoderRelease(enc);

    /* Rich-content pass: anchored composites (yplot/yimage/… wrapped as
     * ydraw composites) scroll with the text. Whatever line currently sits at
     * visible row R draws its owned composites at that row's pixel origin, on
     * top of the text (each composite runs its own LoadOp_Load pass). The
     * vertical scroll offset + visual zoom match the text mapping. This is
     * consumed in the SAME render that clears the dirty flags below, so rich
     * updates are never dropped.
     *
     * NOTE: raw SDF *primitives* stored in the per-line arena are not drawn
     * here yet — that needs the ydraw SDF grid/binder pipeline (the deleted
     * ydraw-scrolling-layer). Composites cover the common anchored-figure case. */
    float zoom = vterm->visual_zoom_scale > 0.0f ? vterm->visual_zoom_scale : 1.0f;
    float pane_height = rect.max.y - rect.min.y;
    /* Canonical visual-zoom transform, identical to the text shader's, so a
     * figure stays welded to its text row under zoom AND pan. center is the pane
     * centre; voff is the SOURCE-space pan; the figure's un-zoomed grid origin
     * maps to screen via  screen = (grid - center - voff) * scale + center. */
    float vz_center_x = (float)cols * vterm->cell_size.width * 0.5f;
    float vz_center_y = (float)rows * vterm->cell_size.height * 0.5f;
    float vz_off_x = vterm->visual_zoom_offset_x;
    float vz_off_y = vterm->visual_zoom_offset_y;
    /* Intrusive (cell-size) zoom grew the cells and reflowed; a figure's reserved
     * rows are now taller, so scale its body by the cell ratio to keep filling
     * them. Visual zoom multiplies on top. */
    float cell_ratio = (vterm->baseline_cell_height > 0.0f)
                           ? vterm->cell_size.height / vterm->baseline_cell_height
                           : 1.0f;
    float figure_scale = zoom * cell_ratio;
    /* Rich SDF pass: the raw ydraw records (ycat PDF/SVG/markdown — SDF shapes,
     * glyphs, text runs) stored per line. Anchored by the same resolved window
     * as the text, so they scroll in lockstep across all tiers. Drawn after
     * the text and BEFORE the anchored composites: producers layer their
     * figures over the SDF chrome (a browser page's images sit on its
     * background rectangles; the old composites-first order painted every
     * page background OVER its images, blanking them). Best-effort — never
     * fail the frame on it. */
    if (vterm->sdf_layer) {
        struct yetty_ycore_void_result sdf_res = yetty_yvterm_sdf_layer_render(
            vterm->sdf_layer, grid, target, rect, vterm->cell_size.width, vterm->cell_size.height,
            cols, rows, window_slots, window_rows, slot_count, zoom, vz_off_x, vz_off_y,
            cell_ratio);
        if (YETTY_IS_ERR(sdf_res)) {
            ywarn("vterm_render: SDF layer: %s", sdf_res.error.msg);
            yetty_ycore_error_destroy(sdf_res.error);
        }
    }

    /* Anchored-composite pass — after the SDF records, so figures render
     * on top of any page chrome sharing their lines (see the SDF pass
     * comment above; a z-aware unified ordering across both record kinds
     * is the eventual replacement for this two-pass split).
     *
     * A figure is anchored on its BOTTOM line and spans upward, so the scan
     * runs the whole resolved window: the visible rows plus the look-ahead
     * BELOW the bottom (already folded into window_rows above) — in a
     * scrolled-back view a figure whose bottom sits below the viewport may
     * still have its top poking into the pane. In the live view the window is
     * exactly the visible rows. */
    for (int row = 0; row < (int)window_rows; ++row) {
        uint32_t comp_count = 0;
        /* Read composites by the SAME window slot the text pass drew at this
         * row, so figures scroll in lockstep with the text in both live and
         * scrolled-back views (including archive rows served from the
         * materialization cache). */
        uint32_t comp_slot = window_slots[row];
        struct yetty_ydraw_composite_const_ptr_ptr_result comps_res =
            yetty_yvterm_grid_slot_composites(grid, comp_slot, &comp_count);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, comps_res, "vterm_render: slot composites");
        struct yetty_ydraw_composite *const *comps = comps_res.value;
        if (!comps || comp_count == 0) {
            continue;
        }
        /* This row is the block's BOTTOM line; recover its top so the figure
         * still draws top-down from where its text sits. span 0 → single row. */
        struct yetty_ycore_uint32_result span_res = yetty_yvterm_grid_slot_span(grid, comp_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, span_res, "vterm_render: slot span");
        uint32_t span = span_res.value;
        int top_row = row - (int)(span ? span - 1u : 0u);
        float anchor_y = (float)top_row * vterm->cell_size.height;
        float comp_x = rect.min.x + (0.0f - vz_center_x - vz_off_x) * zoom + vz_center_x;
        float comp_y_local = (anchor_y - vz_center_y - vz_off_y) * zoom + vz_center_y;
        float comp_y = rect.min.y + comp_y_local;
        for (uint32_t ci = 0; ci < comp_count; ++ci) {
            if (!comps[ci]) {
                continue;
            }
            /* Cull a figure that has fully left the pane: its bottom edge above
             * the pane top (scrolled off the top), or its top below the pane
             * bottom (scrolled off / pushed down under zoom). A figure with no
             * reported height is drawn whenever its top row is on screen. */
            /* Cull on the figure's BOTTOM extent within its block —
			 * bounds.max.y, not the bare height: a multi-figure envelope
			 * (a browser page) positions records deep inside the block, so
			 * a small figure far down must survive the block top scrolling
			 * off. A figure with no reported extent draws whenever its
			 * block top is on screen. */
            float fig_bottom = yetty_ydraw_composite_pixel_bottom(comps[ci]) * figure_scale;
            if (fig_bottom > 0.0f && comp_y_local + fig_bottom <= 0.0f) {
                continue;
            }
            if (comp_y_local >= pane_height) {
                continue;
            }
            /* Magnify the figure body to keep filling its reserved rows: the
             * visual-zoom factor times the cell-size (intrusive-zoom) ratio.
             * Position (comp_x/comp_y) already carries the visual zoom, and the
             * row pitch already carries the larger cell. */
            yetty_ydraw_composite_set_content_scale(comps[ci], figure_scale);
            struct yetty_ycore_void_result cr =
                yetty_ydraw_composite_render(comps[ci], target, comp_x, comp_y);
            if (YETTY_IS_ERR(cr)) {
                ydebug("vterm composite: render FAILED: %s", cr.error.msg);
                yetty_ycore_error_destroy(cr.error);
            }
        }
    }

    /* Shader-glyph pass: animated procedural glyphs for PUA-B cells, drawn on
     * top of the text with the same resolved-window mapping. Best-effort. */
    if (vterm->shader_glyph_layer) {
        struct yetty_ycore_void_result sg_res = yetty_yvterm_shader_glyph_layer_render(
            vterm->shader_glyph_layer, grid, target, rect, vterm->cell_size.width,
            vterm->cell_size.height, cols, rows, window_slots, window_rows, zoom, vz_off_x,
            vz_off_y);
        if (YETTY_IS_ERR(sg_res)) {
            ywarn("vterm_render: shader-glyph layer: %s", sg_res.error.msg);
            yetty_ycore_error_destroy(sg_res.error);
        }
    }

    /* Uploaded — clear model dirty now that the renderer has consumed the text
     * plane, the anchored composites, and the SDF/glyph records. */
    struct yetty_ycore_void_result clear_dirty_res = yetty_yvterm_grid_clear_dirty(grid);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, clear_dirty_res, "vterm_render: clear dirty");
    vterm->view_dirty = 0;
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Figure slots.
 *=========================================================================*/

/* Render the unified model into the figure's target: walk dirty lines, resolve
 * glyphs, upload, draw the grid quad. The figure rect spans the whole pane;
 * the render places the grid on the resolved content rect inside it (a docked
 * HUD reserved part of the pane), clamped so it never escapes the figure. */
YETTY_ANNOTATE("override@yfigure:figure:render")
static struct yetty_ycore_void_result vterm_render_slot(struct yetty_yclass_object *obj,
                                                        struct yetty_ydraw_target *target)
{
    struct yetty_yvterm_vterm_ptr_result vterm_res = yetty_yvterm_vterm_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, vterm_res, "vterm_render: from_obj");
    struct yetty_yvterm_vterm *vterm = vterm_res.value;

    struct rectangle_result rect_res = yetty_yfigure_figure_rect_get(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rect_res, "vterm_render: rect");
    struct yetty_ycore_rectangle rect = rect_res.value;
    if (vterm->content_rect_w > 0.0f && vterm->content_rect_h > 0.0f) {
        struct yetty_ycore_rectangle figure_rect = rect;
        rect.min.x = figure_rect.min.x + vterm->content_rect_x;
        rect.min.y = figure_rect.min.y + vterm->content_rect_y;
        rect.max.x = rect.min.x + vterm->content_rect_w;
        rect.max.y = rect.min.y + vterm->content_rect_h;
        if (rect.max.x > figure_rect.max.x) {
            rect.max.x = figure_rect.max.x;
        }
        if (rect.max.y > figure_rect.max.y) {
            rect.max.y = figure_rect.max.y;
        }
    }

    return vterm_render_grid(vterm, target, rect);
}

YETTY_ANNOTATE("override@yfigure:figure:destroy")
static struct yetty_ycore_void_result vterm_destroy_slot(struct yetty_yclass_object *obj)
{
    struct yetty_yvterm_vterm_ptr_result vterm_res = yetty_yvterm_vterm_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, vterm_res, "vterm_destroy: from_obj");
    vterm_gpu_destroy(vterm_res.value);
    if (vterm_res.value->grid_obj) {
        struct yetty_ycore_void_result gd = yetty_yvterm_grid_dispose(vterm_res.value->grid_obj);
        if (YETTY_IS_ERR(gd)) {
            yetty_ycore_error_destroy(gd.error);
        }
        vterm_res.value->grid_obj = NULL;
    }

    struct yetty_ycore_void_result free_res = yetty_yclass_object_free(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, free_res, "vterm_destroy: object_free");
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Public API — object-keyed; the terminal drives the figure through these.
 *=========================================================================*/

/* Fired after a full-screen erase clears the visible lines, so the terminal can
 * also drop external figures the model does not own. Identical underlying type
 * to grid.c's field. Defined plainly; codegen reproduces it into vterm.h when
 * the exposed setter references it. */
typedef struct yetty_ycore_void_result (*yetty_yvterm_clear_hook_fn)(void *userdata);

/* Re-creates one anchored figure from its retained wire envelope when an
 * evicted history line scrolls back into view. Identical underlying type to
 * grid.c's yetty_yvterm_grid_materialize_fn — a vterm-local typedef for the
 * same reason as the clear hook above (codegen reproduces it into vterm.h). */
typedef struct yetty_ycore_void_result (*yetty_yvterm_materialize_fn)(
    const uint32_t *envelope_words, uint32_t envelope_word_count, void *userdata,
    struct yetty_ydraw_composite **out_instance);

/* Resolve one config colour key to the web-style 0xRRGGBB the grid setters
 * take. Missing / malformed keys fall back to default_hex so a partial
 * palette in config still works. */
static uint32_t vterm_color_config_rgb(const struct yetty_yconfig_config *config, const char *key,
                                       const char *default_hex)
{
    const char *text = NULL;
    if (config && config->ops && config->ops->get_string) {
        text = config->ops->get_string(config, key, default_hex);
    }
    if (!text || !text[0]) {
        text = default_hex;
    }
    uint32_t packed = 0;
    if (!yetty_ycore_parse_hex_color(text, &packed) &&
        !yetty_ycore_parse_hex_color(default_hex, &packed)) {
        return 0;
    }
    /* parse_hex_color packs byte0=R, byte1=G, byte2=B. */
    uint32_t red = packed & 0xFFu;
    uint32_t green = (packed >> 8) & 0xFFu;
    uint32_t blue = (packed >> 16) & 0xFFu;
    return (red << 16) | (green << 8) | blue;
}

/* Apply the configurable terminal colour palette to a freshly-made grid.
 *
 * libvterm resolves every indexed colour (ANSI 30-37/90-97, 38;5;n for n<16)
 * through its 16-entry palette; truecolor (38;2;r;g;b) bypasses it entirely.
 * The built-in defaults are deliberately harsh full-saturation primaries, so
 * they are overridden here with softer values, each refinable via config:
 *   terminal/colors/normal/{black,red,green,yellow,blue,magenta,cyan,white}
 *   terminal/colors/bright/{...same...}
 *   terminal/colors/{foreground,background}
 * Must run before the grid receives PTY data — indexed colours are resolved
 * at parse time, and set_default_colors hard-resets the state. */
static void vterm_apply_color_config(struct yetty_yvterm_vterm *vterm,
                                     const struct yetty_yconfig_config *config)
{
    /* Config key + soft default per palette slot — a muted xterm-style
     * palette (the kind tmux / nvim / ls look right against). */
    static const struct {
        const char *key;
        const char *hex;
    } palette[16] = {
        {"terminal/colors/normal/black", "#1d1f21"}, {"terminal/colors/normal/red", "#cc6666"},
        {"terminal/colors/normal/green", "#b5bd68"}, {"terminal/colors/normal/yellow", "#f0c674"},
        {"terminal/colors/normal/blue", "#81a2be"},  {"terminal/colors/normal/magenta", "#b294bb"},
        {"terminal/colors/normal/cyan", "#8abeb7"},  {"terminal/colors/normal/white", "#c5c8c6"},
        {"terminal/colors/bright/black", "#666666"}, {"terminal/colors/bright/red", "#d54e53"},
        {"terminal/colors/bright/green", "#b9ca4a"}, {"terminal/colors/bright/yellow", "#e7c547"},
        {"terminal/colors/bright/blue", "#7aa6da"},  {"terminal/colors/bright/magenta", "#c397d8"},
        {"terminal/colors/bright/cyan", "#70c0ba"},  {"terminal/colors/bright/white", "#eaeaea"},
    };

    for (uint32_t index = 0; index < 16u; ++index) {
        struct yetty_ycore_void_result palette_res = yetty_yvterm_grid_set_palette_color(
            vterm->grid_obj, index,
            vterm_color_config_rgb(config, palette[index].key, palette[index].hex));
        if (YETTY_IS_ERR(palette_res)) {
            yetty_ycore_error_destroy(palette_res.error);
        }
    }

    /* Default fg/bg keep yetty's existing look unless config overrides them:
     * near-white text on a black canvas (the pane background is painted
     * separately by yui). */
    struct yetty_ycore_void_result defaults_res = yetty_yvterm_grid_set_default_colors(
        vterm->grid_obj, vterm_color_config_rgb(config, "terminal/colors/foreground", "#f0f0f0"),
        vterm_color_config_rgb(config, "terminal/colors/background", "#000000"));
    if (YETTY_IS_ERR(defaults_res)) {
        yetty_ycore_error_destroy(defaults_res.error);
    }
}

/* Create the terminal-content figure for one pane. The rect is set in pixels;
 * cell_size is stored so the terminal can read it immediately after create. The
 * terminal hooks (pty write, render request, mouse subscription) are stored for
 * keyboard output and mouse-mode reporting.
 *
 * cell_size starts at a placeholder until the renderer build-out resolves exact
 * metrics from the active font; the terminal overwrites it on the first resize.
 * `context` is accepted now for that future font setup. */
YETTY_ANNOTATE("expose")
struct yetty_yclass_object_ptr_result yetty_yvterm_vterm_figure_create(
    uint32_t cols, uint32_t rows, const struct yetty_context *context,
    struct yetty_yclass_object *sink)
{
    if (cols == 0 || rows == 0) {
        return YETTY_ERR(yetty_yclass_object_ptr, "yvterm vterm_create: invalid size");
    }

    struct yetty_yclass_ptr_result class_res = yetty_yvterm_vterm_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_res, "yvterm vterm_create: class");

    struct yetty_yclass_object_ptr_result object_res = yetty_yclass_object_alloc(class_res.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, object_res, "yvterm vterm_create: object_alloc");
    struct yetty_yclass_object *obj = object_res.value;

    struct yetty_yvterm_vterm_ptr_result vterm_res = yetty_yvterm_vterm_from(obj);
    if (YETTY_IS_ERR(vterm_res)) {
        struct yetty_ycore_void_result free_res = yetty_yclass_object_free(obj);
        if (YETTY_IS_ERR(free_res)) {
            yetty_ycore_error_destroy(free_res.error);
        }
        return YETTY_ERR(yetty_yclass_object_ptr, "yvterm vterm_create: from_obj", vterm_res);
    }
    struct yetty_yvterm_vterm *vterm = vterm_res.value;

    /* Scrollback depth + hot-window depth from config (`scrollback/lines`,
     * `scrollback/hot-lines`); fall back to the model's built-in defaults when
     * config is unavailable. */
    struct yetty_yconfig_config *config =
        (context && context->runtime) ? context->runtime->config : NULL;
    uint32_t scrollback_rows =
        config ? (uint32_t)config->ops->get_int(config, YETTY_YCONFIG_KEY_SCROLLBACK_LINES, 10000)
               : 0u;
    uint32_t hot_rows =
        config
            ? (uint32_t)config->ops->get_int(config, YETTY_YCONFIG_KEY_SCROLLBACK_HOT_LINES, 2000)
            : 0u;

    /* The model is a separate yvterm:grid object this figure composes/owns. */
    struct yetty_yclass_object_ptr_result grid_res =
        yetty_yvterm_grid_make(cols, rows, scrollback_rows, hot_rows);
    if (YETTY_IS_ERR(grid_res)) {
        struct yetty_ycore_void_result free_res = yetty_yclass_object_free(obj);
        if (YETTY_IS_ERR(free_res)) {
            yetty_ycore_error_destroy(free_res.error);
        }
        return YETTY_ERR(yetty_yclass_object_ptr, "yvterm vterm_create: grid_make", grid_res);
    }
    vterm->grid_obj = grid_res.value;
    /* Warm/cold tier budgets (0 keeps the built-in warm default / unlimited
     * spill file). Best-effort — the grid runs on defaults without them. */
    if (config) {
        uint32_t warm_bytes =
            (uint32_t)config->ops->get_int(config, YETTY_YCONFIG_KEY_SCROLLBACK_WARM_BYTES, 0);
        uint32_t file_max_bytes =
            (uint32_t)config->ops->get_int(config, YETTY_YCONFIG_KEY_SCROLLBACK_FILE_MAX_BYTES, 0);
        struct yetty_ycore_void_result budgets_res =
            yetty_yvterm_grid_set_tier_budgets(vterm->grid_obj, warm_bytes, file_max_bytes);
        if (YETTY_IS_ERR(budgets_res)) {
            yetty_ycore_error_destroy(budgets_res.error);
        }
    }
    /* Per-owner byte accounting for the scroll tiers (yctl `memtags` dump).
     * Best-effort — headless/test contexts have no registry. */
    if (context && context->runtime && context->runtime->memtag_registry) {
        struct yetty_ycore_void_result memtags_res =
            yetty_yvterm_grid_register_memtags(vterm->grid_obj, context->runtime->memtag_registry);
        if (YETTY_IS_ERR(memtags_res)) {
            yetty_ycore_error_destroy(memtags_res.error);
        }
    }
    /* Hand the grid its host sink so it can dispatch pty_write / mouse_sub /
     * clipboard_write / sixel_write back to the terminal. */
    struct yetty_ycore_void_result set_sink_res = yetty_yvterm_grid_set_sink(vterm->grid_obj, sink);
    if (YETTY_IS_ERR(set_sink_res)) {
        struct yetty_ycore_void_result grid_dispose_res =
            yetty_yvterm_grid_dispose(vterm->grid_obj);
        if (YETTY_IS_ERR(grid_dispose_res)) {
            yetty_ycore_error_destroy(grid_dispose_res.error);
        }
        struct yetty_ycore_void_result free_res = yetty_yclass_object_free(obj);
        if (YETTY_IS_ERR(free_res)) {
            yetty_ycore_error_destroy(free_res.error);
        }
        return YETTY_ERR(yetty_yclass_object_ptr, "yvterm vterm_create: set_sink", set_sink_res);
    }
    /* Colour palette + default fg/bg from the terminal/colors config keys —
     * before any PTY data, since indexed colours resolve at parse time.
     * Applied with a NULL config too: the soft built-in defaults replace
     * libvterm's harsh primaries either way. */
    vterm_apply_color_config(vterm, config);
    vterm->grid_size = (struct yetty_ycore_grid_size){.cols = cols, .rows = rows};
    vterm->cell_size = (struct yetty_ycore_pixel_size){.width = 9.0f, .height = 18.0f};
    vterm->sink = sink;
    vterm->visual_zoom_scale = 1.0f;

    /* Best-effort: builds the pipeline/font and overrides cell_size with the
     * font's real metrics. Soft GPU failures are consumed inside and reported as
     * OK (the figure still exists as a blank pane); only an unexpected hard error
     * would surface here. */
    struct yetty_ycore_void_result gpu_init_res = vterm_gpu_init(vterm, context);
    if (YETTY_IS_ERR(gpu_init_res)) {
        vterm_gpu_destroy(vterm);
        struct yetty_ycore_void_result grid_dispose_res =
            yetty_yvterm_grid_dispose(vterm->grid_obj);
        if (YETTY_IS_ERR(grid_dispose_res)) {
            yetty_ycore_error_destroy(grid_dispose_res.error);
        }
        struct yetty_ycore_void_result free_res = yetty_yclass_object_free(obj);
        if (YETTY_IS_ERR(free_res)) {
            yetty_ycore_error_destroy(free_res.error);
        }
        return YETTY_ERR(yetty_yclass_object_ptr, "yvterm vterm_create: gpu_init", gpu_init_res);
    }

    struct yetty_ycore_rectangle rect = {
        .min = {.x = 0.0f, .y = 0.0f},
        .max = {.x = (float)cols * vterm->cell_size.width,
                .y = (float)rows * vterm->cell_size.height},
    };
    struct yetty_ycore_void_result rect_res = yetty_yfigure_figure_rect_set(obj, rect);
    struct yetty_ycore_void_result z_res = YETTY_OK_VOID();
    struct yetty_ycore_void_result dirty_res = YETTY_OK_VOID();
    if (YETTY_IS_OK(rect_res)) {
        z_res = yetty_yfigure_figure_z_set(obj, YETTY_YVTERM_VTERM_Z);
    }
    if (YETTY_IS_OK(z_res)) {
        dirty_res = yetty_yfigure_figure_dirty_set(obj, 1);
    }
    /* On any figure-setup failure, tear down the model + GPU + object so the
     * initialized resources do not leak. */
    if (YETTY_IS_ERR(rect_res) || YETTY_IS_ERR(z_res) || YETTY_IS_ERR(dirty_res)) {
        vterm_gpu_destroy(vterm);
        struct yetty_ycore_void_result gd = yetty_yvterm_grid_dispose(vterm->grid_obj);
        if (YETTY_IS_ERR(gd)) {
            yetty_ycore_error_destroy(gd.error);
        }
        struct yetty_ycore_void_result free_res = yetty_yclass_object_free(obj);
        if (YETTY_IS_ERR(free_res)) {
            yetty_ycore_error_destroy(free_res.error);
        }
        if (YETTY_IS_ERR(rect_res)) {
            return YETTY_ERR(yetty_yclass_object_ptr, "yvterm vterm_create: rect_set", rect_res);
        }
        if (YETTY_IS_ERR(z_res)) {
            return YETTY_ERR(yetty_yclass_object_ptr, "yvterm vterm_create: z_set", z_res);
        }
        return YETTY_ERR(yetty_yclass_object_ptr, "yvterm vterm_create: dirty_set", dirty_res);
    }

    return YETTY_OK(yetty_yclass_object_ptr, obj);
}

/* Upcast to the figure base (first slice in the object). */
YETTY_ANNOTATE("expose")
struct yetty_yfigure_figure_ptr_result yetty_yvterm_vterm_as_figure(struct yetty_yclass_object *obj)
{
    return yetty_yfigure_figure_from(obj);
}

YETTY_ANNOTATE("expose")
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yvterm_vterm_feed(struct yetty_yclass_object *obj,
                                                       const char *bytes, size_t len)
{
    struct yetty_yvterm_vterm_ptr_result vterm_res = yetty_yvterm_vterm_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, vterm_res, "yvterm vterm_feed: from_obj");
    return yetty_yvterm_grid_feed(vterm_res.value->grid_obj, bytes, len);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yvterm_vterm_resize(struct yetty_yclass_object *obj,
                                                         struct yetty_ycore_grid_size grid_size,
                                                         struct yetty_ycore_pixel_size cell_size)
{
    struct yetty_yvterm_vterm_ptr_result vterm_res = yetty_yvterm_vterm_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, vterm_res, "yvterm vterm_resize: from_obj");
    struct yetty_yvterm_vterm *vterm = vterm_res.value;
    if (grid_size.cols == 0 || grid_size.rows == 0) {
        return YETTY_ERR(yetty_ycore_void, "yvterm vterm_resize: invalid dimensions");
    }
    struct yetty_ycore_void_result resize_res =
        yetty_yvterm_grid_resize(vterm->grid_obj, grid_size.cols, grid_size.rows);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, resize_res, "yvterm vterm_resize: grid_resize");
    /* Feed the text-area pixel size to the grid so DEC mode 2048 (in-band window
     * resize) can report it to the child. Grid rows/cols are already updated. */
    struct yetty_ycore_void_result pixel_res = yetty_yvterm_grid_set_pixel_size(
        vterm->grid_obj, (uint32_t)((float)grid_size.cols * cell_size.width),
        (uint32_t)((float)grid_size.rows * cell_size.height));
    YETTY_RETURN_IF_ERR(yetty_ycore_void, pixel_res, "yvterm vterm_resize: set_pixel_size");
    /* Reflow re-bases the grid's absolute row numbering, so any scrolled-back
     * view anchored on the old numbering is stale — snap back to the live tail. */
    /* The grid (single view owner) already ended any scrolled-back view
     * inside its resize — nothing to reset here. */
    vterm->grid_size = grid_size;
    vterm->cell_size = cell_size;
    if (vterm->baseline_cell_height <= 0.0f) {
        vterm->baseline_cell_height = cell_size.height;
    }
    /* Re-scale the glyphs to the new cell. Intrusive (Ctrl+Shift) zoom changes
     * the cell stride and reflows; without this the cells grow but the font
     * stays the same size. set_cell_size re-derives the MSDF render scale (no
     * re-raster — MSDF is resolution-independent) and re-rasterizes raster
     * faces; the next frame's font upload picks it up. Best-effort: a font
     * that can't rescale keeps the old size. */
    for (uint32_t i = 0; i < vterm->face_count; ++i) {
        struct yetty_yfont_ms_font *face_font = vterm->faces[i].font;
        if (face_font && face_font->ops && face_font->ops->set_cell_size) {
            struct yetty_ycore_void_result fcr =
                face_font->ops->set_cell_size(face_font, cell_size);
            if (YETTY_IS_ERR(fcr)) {
                yetty_ycore_error_destroy(fcr.error);
            }
        }
    }
    struct yetty_ycore_rectangle rect = {
        .min = {.x = 0.0f, .y = 0.0f},
        .max = {.x = (float)grid_size.cols * cell_size.width,
                .y = (float)grid_size.rows * cell_size.height},
    };
    struct yetty_ycore_void_result rect_res = yetty_yfigure_figure_rect_set(obj, rect);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rect_res, "yvterm vterm_resize: rect_set");
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct pixel_size_result yetty_yvterm_vterm_cell_size(struct yetty_yclass_object *obj)
{
    struct yetty_yvterm_vterm_ptr_result vterm_res = yetty_yvterm_vterm_from(obj);
    YETTY_RETURN_IF_ERR(pixel_size, vterm_res, "yvterm vterm_cell_size: from_obj");
    return YETTY_OK(pixel_size, vterm_res.value->cell_size);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_int_result yetty_yvterm_vterm_is_dirty(struct yetty_yclass_object *obj)
{
    struct yetty_yvterm_vterm_ptr_result vterm_res = yetty_yvterm_vterm_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, vterm_res, "yvterm vterm_is_dirty: from_obj");
    struct yetty_yvterm_vterm *vterm = vterm_res.value;
    struct yetty_ycore_int_result grid_dirty_res = yetty_yvterm_grid_is_dirty(vterm->grid_obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, grid_dirty_res, "yvterm vterm_is_dirty: grid is_dirty");
    return YETTY_OK(yetty_ycore_int, vterm->view_dirty || grid_dirty_res.value);
}

/* Resolved content rect (figure-rect-local pixels): where the text surface
 * renders inside the pane. The terminal resolves the client's reservation
 * (content-rect / legacy inset envelope) against the pane and pushes the
 * result here. width/height <= 0 restores the full figure rect. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yvterm_vterm_set_content_rect(struct yetty_yclass_object *obj,
                                                                   float x, float y, float width,
                                                                   float height)
{
    struct yetty_yvterm_vterm_ptr_result vterm_res = yetty_yvterm_vterm_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, vterm_res, "yvterm vterm_set_content_rect: from_obj");
    struct yetty_yvterm_vterm *vterm = vterm_res.value;
    vterm->content_rect_x = x;
    vterm->content_rect_y = y;
    vterm->content_rect_w = width;
    vterm->content_rect_h = height;
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yvterm_vterm_get_content_rect(struct yetty_yclass_object *obj,
                                                                   float *out_x, float *out_y,
                                                                   float *out_width,
                                                                   float *out_height)
{
    struct yetty_yvterm_vterm_ptr_result vterm_res = yetty_yvterm_vterm_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, vterm_res, "yvterm vterm_get_content_rect: from_obj");
    struct yetty_yvterm_vterm *vterm = vterm_res.value;
    if (out_x) {
        *out_x = vterm->content_rect_x;
    }
    if (out_y) {
        *out_y = vterm->content_rect_y;
    }
    if (out_width) {
        *out_width = vterm->content_rect_w;
    }
    if (out_height) {
        *out_height = vterm->content_rect_h;
    }
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yvterm_vterm_set_clear_hook(struct yetty_yclass_object *obj,
                                                                 yetty_yvterm_clear_hook_fn fn,
                                                                 void *userdata)
{
    struct yetty_yvterm_vterm_ptr_result vterm_res = yetty_yvterm_vterm_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, vterm_res, "yvterm vterm_set_clear_hook: from_obj");
    return yetty_yvterm_grid_set_clear_hook(vterm_res.value->grid_obj,
                                            (yetty_yvterm_grid_clear_hook_fn)fn, userdata);
}

/* Register the figure re-materialization hook on the composed grid model: the
 * terminal (which owns the composite factory) supplies a function that replays
 * a retained wire envelope into a fresh figure instance when an evicted
 * history line scrolls back into view. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yvterm_vterm_set_materialize(struct yetty_yclass_object *obj,
                                                                  yetty_yvterm_materialize_fn fn,
                                                                  void *userdata)
{
    struct yetty_yvterm_vterm_ptr_result vterm_res = yetty_yvterm_vterm_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, vterm_res, "yvterm vterm_set_materialize: from_obj");
    return yetty_yvterm_grid_set_materialize(vterm_res.value->grid_obj,
                                             (yetty_yvterm_grid_materialize_fn)fn, userdata);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yvterm_vterm_cursor(struct yetty_yclass_object *obj,
                                                         uint32_t *out_row, uint32_t *out_col,
                                                         uint32_t *out_visible)
{
    struct yetty_yvterm_vterm_ptr_result vterm_res = yetty_yvterm_vterm_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, vterm_res, "yvterm vterm_cursor: from_obj");
    return yetty_yvterm_grid_cursor(vterm_res.value->grid_obj, out_row, out_col, out_visible);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yvterm_vterm_word_bounds(struct yetty_yclass_object *obj,
                                                              uint32_t row, uint32_t col,
                                                              uint32_t *out_start_col,
                                                              uint32_t *out_end_col)
{
    struct yetty_yvterm_vterm_ptr_result vterm_res = yetty_yvterm_vterm_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, vterm_res, "yvterm vterm_word_bounds: from_obj");
    return yetty_yvterm_grid_word_bounds(vterm_res.value->grid_obj, row, col, out_start_col,
                                         out_end_col);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_uint64_result yetty_yvterm_vterm_scroll_origin(struct yetty_yclass_object *obj)
{
    struct yetty_yvterm_vterm_ptr_result vterm_res = yetty_yvterm_vterm_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint64, vterm_res, "yvterm vterm_scroll_origin: from_obj");
    return yetty_yvterm_grid_scroll_origin(vterm_res.value->grid_obj);
}

/*===========================================================================
 * Rich-content insertion — delegated to the grid model.
 *=========================================================================*/

YETTY_ANNOTATE("expose")
struct yetty_ycore_uint32_result yetty_yvterm_vterm_append_primitive(
    struct yetty_yclass_object *obj, uint32_t row, const uint32_t *words, uint32_t word_count)
{
    struct yetty_yvterm_vterm_ptr_result vterm_res = yetty_yvterm_vterm_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint32, vterm_res, "yvterm append_primitive: from_obj");
    return yetty_yvterm_grid_append_primitive(vterm_res.value->grid_obj, row, words, word_count);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_uint32_result yetty_yvterm_vterm_attach_composite(
    struct yetty_yclass_object *obj, uint32_t row, struct yetty_ydraw_composite *composite)
{
    struct yetty_yvterm_vterm_ptr_result vterm_res = yetty_yvterm_vterm_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint32, vterm_res, "yvterm attach_composite: from_obj");
    return yetty_yvterm_grid_attach_composite(vterm_res.value->grid_obj, row, composite);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yvterm_vterm_relocate_rich_to_bottom(
    struct yetty_yclass_object *obj, uint32_t span_rows)
{
    struct yetty_yvterm_vterm_ptr_result vterm_res = yetty_yvterm_vterm_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, vterm_res, "yvterm relocate_rich: from_obj");
    return yetty_yvterm_grid_relocate_rich_to_bottom(vterm_res.value->grid_obj, span_rows);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yvterm_vterm_clear_rich_line(struct yetty_yclass_object *obj,
                                                                  uint32_t row)
{
    struct yetty_yvterm_vterm_ptr_result vterm_res = yetty_yvterm_vterm_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, vterm_res, "yvterm clear_rich_line: from_obj");
    return yetty_yvterm_grid_clear_rich_line(vterm_res.value->grid_obj, row);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yvterm_vterm_clear_rich_all(struct yetty_yclass_object *obj)
{
    struct yetty_yvterm_vterm_ptr_result vterm_res = yetty_yvterm_vterm_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, vterm_res, "yvterm clear_rich_all: from_obj");
    return yetty_yvterm_grid_clear_rich_all(vterm_res.value->grid_obj);
}

/*===========================================================================
 * Input, wire, selection, scrollback, zoom — delegated to the grid model
 * (except the renderer-local view/zoom state).
 *=========================================================================*/

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yvterm_vterm_register_wire(
    struct yetty_yclass_object *obj, struct yetty_ywire_wire_statemachine *sm)
{
    struct yetty_yvterm_vterm_ptr_result vterm_res = yetty_yvterm_vterm_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, vterm_res, "yvterm register_wire: from_obj");
    return yetty_yvterm_grid_register_wire(vterm_res.value->grid_obj, sm);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_int_result yetty_yvterm_vterm_on_char(struct yetty_yclass_object *obj,
                                                         uint32_t codepoint, int mods)
{
    struct yetty_yvterm_vterm_ptr_result vterm_res = yetty_yvterm_vterm_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, vterm_res, "yvterm vterm_on_char: from_obj");
    return yetty_yvterm_grid_on_char(vterm_res.value->grid_obj, codepoint, mods);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_int_result yetty_yvterm_vterm_on_key(struct yetty_yclass_object *obj, int key,
                                                        int mods)
{
    struct yetty_yvterm_vterm_ptr_result vterm_res = yetty_yvterm_vterm_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, vterm_res, "yvterm vterm_on_key: from_obj");
    return yetty_yvterm_grid_on_key(vterm_res.value->grid_obj, key, mods);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yvterm_vterm_set_selection(struct yetty_yclass_object *obj,
                                                                int active, uint32_t anchor_row,
                                                                uint32_t anchor_col,
                                                                uint32_t head_row,
                                                                uint32_t head_col)
{
    struct yetty_yvterm_vterm_ptr_result vterm_res = yetty_yvterm_vterm_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, vterm_res, "yvterm set_selection: from_obj");
    return yetty_yvterm_grid_set_selection(vterm_res.value->grid_obj, active, anchor_row,
                                           anchor_col, head_row, head_col);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yvterm_vterm_get_selection_text(
    struct yetty_yclass_object *obj, struct yetty_ycore_buffer *out)
{
    struct yetty_yvterm_vterm_ptr_result vterm_res = yetty_yvterm_vterm_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, vterm_res, "yvterm get_selection_text: from_obj");
    return yetty_yvterm_grid_get_selection_text(vterm_res.value->grid_obj, out);
}

/* Timeline index of the line at the live screen top: everything scrolled off
 * so far. A wheel-up anchors one line below this and walks toward the floor. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_uint64_result yetty_yvterm_vterm_get_live_anchor(struct yetty_yclass_object *obj)
{
    struct yetty_yvterm_vterm_ptr_result vterm_res = yetty_yvterm_vterm_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint64, vterm_res, "yvterm vterm_get_live_anchor: from_obj");
    struct yetty_yvterm_vterm *vterm = vterm_res.value;
    if (!vterm->grid_obj) {
        return YETTY_OK(yetty_ycore_uint64, 0u);
    }
    return yetty_yvterm_grid_live_anchor(vterm->grid_obj);
}

/* Oldest timeline index still reachable across the scrollback tiers (hot ring
 * → warm lz4 segments → cold spill file). A wheel-up clamps here. With the
 * cold tier unbounded this is 0 for the whole session. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_uint64_result yetty_yvterm_vterm_get_scrollback_floor(
    struct yetty_yclass_object *obj)
{
    struct yetty_yvterm_vterm_ptr_result vterm_res = yetty_yvterm_vterm_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint64, vterm_res,
                        "yvterm vterm_get_scrollback_floor: from_obj");
    struct yetty_yvterm_vterm *vterm = vterm_res.value;
    if (!vterm->grid_obj) {
        return YETTY_OK(yetty_ycore_uint64, 0u);
    }
    return yetty_yvterm_grid_history_floor(vterm->grid_obj);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yvterm_vterm_set_view_top(struct yetty_yclass_object *obj,
                                                               int active,
                                                               uint64_t view_top_total_idx)
{
    struct yetty_yvterm_vterm_ptr_result vterm_res = yetty_yvterm_vterm_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, vterm_res, "yvterm set_view_top: from_obj");
    struct yetty_yvterm_vterm *vterm = vterm_res.value;
    vterm->view_dirty = 1;
    /* The grid is the single view owner; this figure only forwards. */
    if (vterm->grid_obj) {
        struct yetty_ycore_void_result view_res =
            yetty_yvterm_grid_set_view(vterm->grid_obj, active, view_top_total_idx);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, view_res, "yvterm set_view_top: grid set_view");
    }
    struct yetty_ycore_void_result dr = yetty_yfigure_figure_dirty_set(obj, 1);
    if (YETTY_IS_ERR(dr)) {
        yetty_ycore_error_destroy(dr.error);
    }
    return YETTY_OK_VOID();
}

/* Current scrollback view state, read from the grid (the single owner). The
 * terminal's wheel driver derives every transition from this instead of
 * holding its own copy. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yvterm_vterm_get_view(struct yetty_yclass_object *obj,
                                                           int *out_active, uint64_t *out_view_top)
{
    struct yetty_yvterm_vterm_ptr_result vterm_res = yetty_yvterm_vterm_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, vterm_res, "yvterm get_view: from_obj");
    struct yetty_yvterm_vterm *vterm = vterm_res.value;
    if (!vterm->grid_obj) {
        if (out_active) {
            *out_active = 0;
        }
        if (out_view_top) {
            *out_view_top = 0;
        }
        return YETTY_OK_VOID();
    }
    return yetty_yvterm_grid_view(vterm->grid_obj, out_active, out_view_top);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yvterm_vterm_set_visual_zoom(struct yetty_yclass_object *obj,
                                                                  float scale, float offset_x,
                                                                  float offset_y)
{
    struct yetty_yvterm_vterm_ptr_result vterm_res = yetty_yvterm_vterm_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, vterm_res, "yvterm set_visual_zoom: from_obj");
    struct yetty_yvterm_vterm *vterm = vterm_res.value;
    vterm->visual_zoom_scale = scale;
    vterm->visual_zoom_offset_x = offset_x;
    vterm->visual_zoom_offset_y = offset_y;
    vterm->view_dirty = 1;
    struct yetty_ycore_void_result dr = yetty_yfigure_figure_dirty_set(obj, 1);
    if (YETTY_IS_ERR(dr)) {
        yetty_ycore_error_destroy(dr.error);
    }
    return YETTY_OK_VOID();
}

/* Set the OSC-driven post-color effect for the terminal text surface. index 0
 * disables it; params are the 6 effect parameters (unused ones may be 0). */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yvterm_vterm_set_post_effect(struct yetty_yclass_object *obj,
                                                                  uint32_t index, float p0,
                                                                  float p1, float p2, float p3,
                                                                  float p4, float p5)
{
    struct yetty_yvterm_vterm_ptr_result vterm_res = yetty_yvterm_vterm_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, vterm_res, "yvterm set_post_effect: from_obj");
    struct yetty_yvterm_vterm *vterm = vterm_res.value;
    vterm->post_fx_index = index;
    vterm->post_fx_params[0] = p0;
    vterm->post_fx_params[1] = p1;
    vterm->post_fx_params[2] = p2;
    vterm->post_fx_params[3] = p3;
    vterm->post_fx_params[4] = p4;
    vterm->post_fx_params[5] = p5;
    vterm->view_dirty = 1;
    struct yetty_ycore_void_result dr = yetty_yfigure_figure_dirty_set(obj, 1);
    if (YETTY_IS_ERR(dr)) {
        yetty_ycore_error_destroy(dr.error);
    }
    return YETTY_OK_VOID();
}

/* Set the OSC-driven coordinate distortion for the terminal text surface.
 * index 0 disables it; p0/p1 are strength and radius/aspect. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yvterm_vterm_set_coord_effect(struct yetty_yclass_object *obj,
                                                                   uint32_t index, float p0,
                                                                   float p1, float p2, float p3,
                                                                   float p4, float p5)
{
    struct yetty_yvterm_vterm_ptr_result vterm_res = yetty_yvterm_vterm_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, vterm_res, "yvterm set_coord_effect: from_obj");
    struct yetty_yvterm_vterm *vterm = vterm_res.value;
    vterm->coord_fx_index = index;
    vterm->coord_fx_params[0] = p0;
    vterm->coord_fx_params[1] = p1;
    vterm->coord_fx_params[2] = p2;
    vterm->coord_fx_params[3] = p3;
    vterm->coord_fx_params[4] = p4;
    vterm->coord_fx_params[5] = p5;
    vterm->view_dirty = 1;
    struct yetty_ycore_void_result dr = yetty_yfigure_figure_dirty_set(obj, 1);
    if (YETTY_IS_ERR(dr)) {
        yetty_ycore_error_destroy(dr.error);
    }
    return YETTY_OK_VOID();
}

/* Report the live pointer position (pane-local pixels) so mouse-following
 * effects track it. Only forces a repaint while a coordinate effect is active
 * — otherwise pointer motion must not drive frames. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yvterm_vterm_set_mouse(struct yetty_yclass_object *obj,
                                                            float x, float y)
{
    struct yetty_yvterm_vterm_ptr_result vterm_res = yetty_yvterm_vterm_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, vterm_res, "yvterm set_mouse: from_obj");
    struct yetty_yvterm_vterm *vterm = vterm_res.value;
    vterm->mouse_local_x = x;
    vterm->mouse_local_y = y;
    vterm->have_mouse = 1;
    if (vterm->coord_fx_index != 0u) {
        vterm->view_dirty = 1;
        struct yetty_ycore_void_result dr = yetty_yfigure_figure_dirty_set(obj, 1);
        if (YETTY_IS_ERR(dr)) {
            yetty_ycore_error_destroy(dr.error);
        }
        if (vterm->sink) {
            struct yetty_ycore_void_result rr = yetty_ytermsink_request_render(vterm->sink);
            if (YETTY_IS_ERR(rr)) {
                yetty_ycore_error_destroy(rr.error);
            }
        }
    }
    return YETTY_OK_VOID();
}

#include "yetty/gen/impl/yvterm/vterm.c"
