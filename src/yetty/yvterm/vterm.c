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
#include <yetty/yetty/yetty.h>
#include <yetty/yfigure/figure.h>
#include <yetty/yfont/ms-font.h>
#include <yetty/yfont/ms-msdf-font.h>
#include <yetty/yframework/yframework.h>
#include <yetty/yrender/gpu-resource-set.h>
#include <yetty/yrender/render-target.h>
#include <yetty/yterminal/terminal.h>
#include <yetty/ytrace/ytrace.h>
#include <yetty/yvterm/grid.h>
#include <yetty/ywire/wire-statemachine.h>

#include "sdf-layer.h"

/* GPU cell layout the text shader reads: 4 u32 words per cell. */
enum { YVTERM_WORDS_PER_CELL = 4 };

/* How many rows ABOVE the visible top the rich pass scans for figure anchors, on
 * top of one screen height. An anchored figure spans downward from its top line,
 * so once that top line scrolls into history the figure must keep drawing until
 * its bottom row also leaves; the scan therefore reaches back far enough to find
 * the anchor of any figure taller than the screen. Past this, a figure is fully
 * off the top. The per-figure extent test culls anything that doesn't reach. */
enum { YVTERM_COMPOSITE_LOOKBACK_ROWS = 256 };

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
    uint32_t pad_b;
    uint32_t pad_c;
    uint32_t pad_d;
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

struct [[clang::annotate("class@yvterm:vterm"),
         clang::annotate("include@yetty/yterminal/terminal.h")]] [[clang::annotate(
    "parent@yfigure:figure")]] yetty_yvterm_vterm {
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

    /* Content insets in pane-local pixels — a docked status bar / HUD reserved
     * a band of the pane; the render slot narrows its viewport to the rest. */
    float content_inset_top;
    float content_inset_right;
    float content_inset_bottom;
    float content_inset_left;

    /* Terminal-owned hooks. request_render asks the terminal for a frame;
     * mouse_sub reports libvterm mouse-mode changes. (pty_write lives on the
     * grid model, which produces the keyboard/query output.) */
    yetty_yterminal_request_render_fn request_render_fn;
    void *request_render_userdata;
    yetty_yterminal_mouse_sub_fn mouse_sub_fn;
    void *mouse_sub_userdata;

    /* Scrollback view (not yet backed by a scrollback ring — see methods). */
    int view_top_active;
    uint32_t view_top_total_idx;

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
    WGPUDevice device;
    WGPUQueue queue;
    WGPUTextureFormat target_format;
    struct yetty_yfont_ms_font *font;

    /* Owned wgpu resources. */
    WGPUShaderModule shader_module;
    WGPUBindGroupLayout bind_group_layout;
    WGPURenderPipeline pipeline;
    WGPUSampler sampler;
    WGPUBuffer cell_buffer;
    WGPUBuffer meta_buffer;
    WGPUBuffer uniform_buffer;
    WGPUTexture atlas_texture;
    WGPUTextureView atlas_view;
    uint32_t atlas_width;
    uint32_t atlas_height;
    size_t meta_capacity;
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
};

/* Result wrapper for the vterm handle. Declared here (this TU does not include
 * its own generated header); vterm.h publishes the identical declaration. */
YETTY_YRESULT_DECLARE(yetty_yvterm_vterm_ptr, struct yetty_yvterm_vterm *);

/* Defined in the appended vterm.gen.c. */
struct yetty_yclass_ptr_result yetty_yvterm_vterm_class_get(void);
struct yetty_yvterm_vterm_ptr_result yetty_yvterm_vterm_from(struct yetty_yclass_object *obj);

/* Resolve obj → body, absorbing the error into NULL for void/scalar getters. */
static struct yetty_yvterm_vterm *vterm_body_or_null(struct yetty_yclass_object *obj)
{
    if (!obj) {
        return NULL;
    }
    struct yetty_yvterm_vterm_ptr_result vterm_res = yetty_yvterm_vterm_from(obj);
    if (YETTY_IS_ERR(vterm_res)) {
        yetty_ycore_error_destroy(vterm_res.error);
        return NULL;
    }
    return vterm_res.value;
}

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
        "    pad_b: u32, pad_c: u32, pad_d: u32,\n"
        "};\n"
        "@group(0) @binding(0) var<storage, read> cells: array<u32>;\n"
        "@group(0) @binding(1) var<storage, read> glyph_meta: array<u32>;\n"
        "@group(0) @binding(2) var atlas_tex: texture_2d<f32>;\n"
        "@group(0) @binding(3) var atlas_smp: sampler;\n"
        "@group(0) @binding(4) var<uniform> uni: Uniforms;\n"
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
        "fn sample_glyph(glyph: u32, local_px: vec2<f32>) -> f32 {\n"
        "    let base = glyph * 10u;\n"
        "    let uv_min = vec2<f32>(bitcast<f32>(glyph_meta[base+0u]), "
        "bitcast<f32>(glyph_meta[base+1u]));\n"
        "    let uv_max = vec2<f32>(bitcast<f32>(glyph_meta[base+2u]), "
        "bitcast<f32>(glyph_meta[base+3u]));\n"
        "    let gsize = vec2<f32>(bitcast<f32>(glyph_meta[base+4u]), "
        "bitcast<f32>(glyph_meta[base+5u]));\n"
        "    let bear = vec2<f32>(bitcast<f32>(glyph_meta[base+6u]), "
        "bitcast<f32>(glyph_meta[base+7u]));\n"
        "    if (gsize.x <= 0.0 || gsize.y <= 0.0) { return 0.0; }\n"
        "    let scaled_size = gsize * uni.scale;\n"
        "    let scaled_bear = bear * uni.scale;\n"
        "    let gtop = uni.baseline_y - scaled_bear.y;\n"
        "    let gleft = uni.glyph_left + scaled_bear.x;\n"
        "    let gmin = vec2<f32>(gleft, gtop);\n"
        "    let gmax = vec2<f32>(gleft + scaled_size.x, gtop + scaled_size.y);\n"
        "    if (local_px.x < gmin.x || local_px.x >= gmax.x || local_px.y < gmin.y || "
        "local_px.y >= gmax.y) { return 0.0; }\n"
        "    let gl = (local_px - gmin) / scaled_size;\n"
        "    let uv = mix(uv_min, uv_max, gl);\n"
        "    let texel = textureSampleLevel(atlas_tex, atlas_smp, uv, 0.0);\n"
        "    let sd = median3(texel.r, texel.g, texel.b);\n"
        "    let screen_px_range = uni.pixel_range * uni.scale;\n"
        "    return clamp((sd - 0.5) * screen_px_range + 0.5, 0.0, 1.0);\n"
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
        "    let px = (in.grid_pixel - center) / vz + center + voff;\n"
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
        "    if ((cells[cell_index*4u + 3u] & 0xFFu) == 0u && col > 0u) {\n"
        "        let head = slot * gcols + (col - 1u);\n"
        "        if ((cells[head*4u + 3u] & 0xFFu) == 2u) {\n"
        "            glyph = cells[head*4u + 0u];\n"
        "            local.x = local.x + uni.cell_size.x;\n"
        "        }\n"
        "    }\n"
        "    var alpha = 0.0;\n"
        "    if (glyph != 0u) { alpha = sample_glyph(glyph, local); }\n"
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
        "    if (is_cursor || selected) {\n"
        "        composed = mix(fg, bg, alpha);\n" /* inverted: fg fill, glyph punched in bg */
        "    }\n"
        "    return vec4<f32>(composed, 1.0);\n"
        "}\n";
    return src;
}

/* Resolve a codepoint to an atlas glyph index for the given style. The styled
 * lookup lazy-loads the (style, codepoint) slot on demand, falling back to the
 * Regular face when a bold/italic CDB variant is absent. */
static uint32_t vterm_resolve_glyph(struct yetty_yvterm_vterm *vterm, uint32_t codepoint,
                                    enum yetty_yfont_ms_style style)
{
    if (!vterm->font || codepoint == 0) {
        return 0;
    }
    struct uint32_result gi =
        vterm->font->ops->get_glyph_index_styled(vterm->font, codepoint, style);
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
    for (uint32_t col = 0; col < cols; ++col) {
        const struct yetty_yvterm_text_cell *cell = &cells[col];
        /* Concealed cells render their background only — resolve no glyph.
         * Bold/italic pick a styled atlas slot via the cell attributes. */
        uint32_t glyph = 0u;
        if (cell->codepoint && !(cell->attrs & YETTY_YVTERM_ATTR_CONCEAL)) {
            glyph = vterm_resolve_glyph(vterm, cell->codepoint, vterm_cell_style(cell->attrs));
        }
        uint32_t fg = cell->fg;
        uint32_t bg = cell->bg;
        uint32_t fr = fg & 0xFFu, fgr = (fg >> 8) & 0xFFu, fb = (fg >> 16) & 0xFFu;
        uint32_t br = bg & 0xFFu, bgr = (bg >> 8) & 0xFFu, bb = (bg >> 16) & 0xFFu;
        uint32_t *words = &out[(size_t)col * YVTERM_WORDS_PER_CELL];
        words[0] = glyph;
        words[1] = fr | (fgr << 8) | (fb << 16) | (br << 24);
        words[2] = bgr | (bb << 8) | ((uint32_t)cell->attrs << 16);
        words[3] = cell->width;
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

static struct yetty_ycore_void_result vterm_create_pipeline(struct yetty_yvterm_vterm *vterm)
{
    WGPUShaderSourceWGSL wgsl = {0};
    wgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgsl.code = vterm_sv(vterm_text_wgsl());
    WGPUShaderModuleDescriptor shader_desc = {0};
    shader_desc.nextInChain = &wgsl.chain;
    shader_desc.label = vterm_sv("yvterm text shader");
    vterm->shader_module = wgpuDeviceCreateShaderModule(vterm->device, &shader_desc);
    if (!vterm->shader_module) {
        return YETTY_ERR(yetty_ycore_void, "vterm: shader module");
    }

    WGPUBindGroupLayoutEntry entries[5] = {0};
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
    WGPUBindGroupLayoutDescriptor bgl_desc = {0};
    bgl_desc.entryCount = 5;
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
    yetty_yvterm_grid_dims(vterm->grid_obj, &cols, &rows, NULL);
    /* The buffer is ring-slot-indexed and must cover the FULL ring (visible +
     * scrollback), not just the visible rows, so retained history stays
     * resident for the scrolled-back view. */
    uint32_t ring_rows = yetty_yvterm_grid_slot_count(vterm->grid_obj);
    if (ring_rows < rows) {
        ring_rows = rows;
    }
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

static struct yetty_ycore_void_result vterm_recreate_atlas(struct yetty_yvterm_vterm *vterm,
                                                           uint32_t width, uint32_t height)
{
    if (vterm->atlas_view) {
        wgpuTextureViewRelease(vterm->atlas_view);
        vterm->atlas_view = NULL;
    }
    if (vterm->atlas_texture) {
        wgpuTextureDestroy(vterm->atlas_texture);
        wgpuTextureRelease(vterm->atlas_texture);
        vterm->atlas_texture = NULL;
    }
    WGPUTextureDescriptor tex_desc = {0};
    tex_desc.label = vterm_sv("yvterm atlas");
    tex_desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    tex_desc.dimension = WGPUTextureDimension_2D;
    tex_desc.size.width = width;
    tex_desc.size.height = height;
    tex_desc.size.depthOrArrayLayers = 1;
    tex_desc.format = WGPUTextureFormat_RGBA8Unorm;
    tex_desc.mipLevelCount = 1;
    tex_desc.sampleCount = 1;
    vterm->atlas_texture = wgpuDeviceCreateTexture(vterm->device, &tex_desc);
    if (!vterm->atlas_texture) {
        return YETTY_ERR(yetty_ycore_void, "vterm: atlas texture");
    }
    vterm->atlas_view = wgpuTextureCreateView(vterm->atlas_texture, NULL);
    if (!vterm->atlas_view) {
        return YETTY_ERR(yetty_ycore_void, "vterm: atlas view");
    }
    vterm->atlas_width = width;
    vterm->atlas_height = height;
    vterm->bind_group_valid = 0;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result vterm_recreate_meta(struct yetty_yvterm_vterm *vterm,
                                                          size_t size)
{
    if (vterm->meta_buffer) {
        wgpuBufferDestroy(vterm->meta_buffer);
        wgpuBufferRelease(vterm->meta_buffer);
        vterm->meta_buffer = NULL;
    }
    WGPUBufferDescriptor desc = {0};
    desc.label = vterm_sv("yvterm meta");
    desc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
    desc.size = size;
    vterm->meta_buffer = wgpuDeviceCreateBuffer(vterm->device, &desc);
    if (!vterm->meta_buffer) {
        return YETTY_ERR(yetty_ycore_void, "vterm: meta buffer");
    }
    vterm->meta_capacity = size;
    vterm->bind_group_valid = 0;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result vterm_upload_font(struct yetty_yvterm_vterm *vterm)
{
    struct yetty_yrender_gpu_resource_set_result rs_res =
        vterm->font->ops->get_gpu_resource_set(vterm->font);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rs_res, "vterm_upload_font: get_gpu_resource_set");
    const struct yetty_yrender_gpu_resource_set *rs = rs_res.value;

    vterm->uniforms.pixel_range = rs->uniforms[0].f32;
    vterm->uniforms.scale = rs->uniforms[1].f32;
    vterm->uniforms.baseline_y = rs->uniforms[2].f32;
    vterm->uniforms.glyph_left = rs->uniforms[3].f32;

    const struct yetty_yrender_texture *atlas = &rs->textures[0];
    if (atlas->data && atlas->width > 0 && atlas->height > 0) {
        if (!vterm->atlas_texture || atlas->width != vterm->atlas_width ||
            atlas->height != vterm->atlas_height) {
            struct yetty_ycore_void_result re =
                vterm_recreate_atlas(vterm, atlas->width, atlas->height);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, re, "vterm_upload_font: recreate_atlas");
        }
        WGPUTexelCopyTextureInfo dest = {0};
        dest.texture = vterm->atlas_texture;
        dest.mipLevel = 0;
        WGPUTexelCopyBufferLayout src_layout = {0};
        src_layout.bytesPerRow = atlas->width * 4u;
        src_layout.rowsPerImage = atlas->height;
        WGPUExtent3D extent = {atlas->width, atlas->height, 1};
        size_t bytes = (size_t)atlas->width * atlas->height * 4u;
        wgpuQueueWriteTexture(vterm->queue, &dest, atlas->data, bytes, &src_layout, &extent);
    }

    const struct yetty_yrender_buffer *meta = &rs->buffers[0];
    if (meta->data && meta->size > 0) {
        if (!vterm->meta_buffer || meta->size > vterm->meta_capacity) {
            struct yetty_ycore_void_result re = vterm_recreate_meta(vterm, meta->size);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, re, "vterm_upload_font: recreate_meta");
        }
        wgpuQueueWriteBuffer(vterm->queue, vterm->meta_buffer, 0, meta->data, meta->size);
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
    if (!vterm->cell_buffer || !vterm->meta_buffer || !vterm->atlas_view || !vterm->sampler ||
        !vterm->uniform_buffer) {
        return YETTY_ERR(yetty_ycore_void, "vterm_ensure_bind_group: missing resource");
    }
    WGPUBindGroupEntry entries[5] = {0};
    entries[0].binding = 0;
    entries[0].buffer = vterm->cell_buffer;
    entries[0].size = WGPU_WHOLE_SIZE;
    entries[1].binding = 1;
    entries[1].buffer = vterm->meta_buffer;
    entries[1].size = WGPU_WHOLE_SIZE;
    entries[2].binding = 2;
    entries[2].textureView = vterm->atlas_view;
    entries[3].binding = 3;
    entries[3].sampler = vterm->sampler;
    entries[4].binding = 4;
    entries[4].buffer = vterm->uniform_buffer;
    entries[4].size = sizeof(struct vterm_uniforms);
    WGPUBindGroupDescriptor bg_desc = {0};
    bg_desc.layout = vterm->bind_group_layout;
    bg_desc.entryCount = 5;
    bg_desc.entries = entries;
    vterm->bind_group = wgpuDeviceCreateBindGroup(vterm->device, &bg_desc);
    if (!vterm->bind_group) {
        return YETTY_ERR(yetty_ycore_void, "vterm_ensure_bind_group: create");
    }
    vterm->bind_group_valid = 1;
    return YETTY_OK_VOID();
}

/* Best-effort GPU setup. On failure leaves gpu_ready=0 so the figure still
 * exists (blank pane) rather than failing terminal creation. */
static void vterm_gpu_init(struct yetty_yvterm_vterm *vterm, const struct yetty_context *context)
{
    if (!context || !context->runtime) {
        return;
    }
    struct yetty_yframework *runtime = context->runtime;
    vterm->device = runtime->gpu.device;
    vterm->queue = runtime->gpu.queue;
    vterm->target_format = runtime->gpu.surface_format;
    if (!vterm->device || !vterm->queue) {
        return;
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

    char cdb_path[768];
    char font_shader_path[768];
    snprintf(cdb_path, sizeof(cdb_path), "%s/../msdf-fonts/DejaVuSansMNerdFontMono-Regular.cdb",
             fonts_dir);
    snprintf(font_shader_path, sizeof(font_shader_path), "%s/ms-msdf-font.wgsl", shaders_dir);
    struct yetty_yfont_ms_padding padding = {0};
    struct yetty_font_ms_font_result font_res =
        yetty_yfont_ms_msdf_font_create(cdb_path, font_shader_path, font_size, padding);
    if (YETTY_IS_ERR(font_res)) {
        ywarn("vterm: font create failed (%s): %s", cdb_path, font_res.error.msg);
        yetty_ycore_error_destroy(font_res.error);
        return;
    }
    vterm->font = font_res.value;
    struct yetty_ycore_void_result latin = vterm->font->ops->load_basic_latin(vterm->font);
    if (YETTY_IS_ERR(latin)) {
        yetty_ycore_error_destroy(latin.error);
    }
    struct pixel_size_result cell = vterm->font->ops->get_cell_size(vterm->font);
    if (YETTY_IS_OK(cell)) {
        vterm->cell_size = cell.value;
    }

    struct yetty_ycore_void_result pr = vterm_create_pipeline(vterm);
    if (YETTY_IS_ERR(pr)) {
        ywarn("vterm: pipeline init failed: %s", pr.error.msg);
        yetty_ycore_error_destroy(pr.error);
        return;
    }
    struct yetty_ycore_void_result cbr = vterm_create_cell_buffer(vterm);
    if (YETTY_IS_ERR(cbr)) {
        yetty_ycore_error_destroy(cbr.error);
        return;
    }
    struct yetty_ycore_void_result ufr = vterm_upload_font(vterm);
    if (YETTY_IS_ERR(ufr)) {
        yetty_ycore_error_destroy(ufr.error);
        return;
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

    vterm->gpu_ready = 1;
}

static void vterm_gpu_destroy(struct yetty_yvterm_vterm *vterm)
{
    if (vterm->sdf_layer) {
        yetty_yvterm_sdf_layer_destroy(vterm->sdf_layer);
        vterm->sdf_layer = NULL;
    }
    if (vterm->bind_group) {
        wgpuBindGroupRelease(vterm->bind_group);
        vterm->bind_group = NULL;
    }
    if (vterm->atlas_view) {
        wgpuTextureViewRelease(vterm->atlas_view);
        vterm->atlas_view = NULL;
    }
    if (vterm->atlas_texture) {
        wgpuTextureDestroy(vterm->atlas_texture);
        wgpuTextureRelease(vterm->atlas_texture);
        vterm->atlas_texture = NULL;
    }
    if (vterm->meta_buffer) {
        wgpuBufferDestroy(vterm->meta_buffer);
        wgpuBufferRelease(vterm->meta_buffer);
        vterm->meta_buffer = NULL;
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
    free(vterm->row_scratch);
    vterm->row_scratch = NULL;
    vterm->row_scratch_cols = 0;
    if (vterm->font) {
        vterm->font->ops->destroy(vterm->font);
        vterm->font = NULL;
    }
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
    uint32_t cols = 0, rows = 0, base = 0;
    yetty_yvterm_grid_dims(grid, &cols, &rows, &base);
    if (cols == 0 || rows == 0) {
        return YETTY_OK_VOID();
    }
    uint32_t slot_count = yetty_yvterm_grid_slot_count(grid);
    /* The model may have been resized since the cell buffer was sized. The buffer
     * covers the full ring (visible + scrollback), so compare against that. */
    if (vterm->cell_buffer_cells != slot_count * cols) {
        struct yetty_ycore_void_result br = vterm_create_cell_buffer(vterm);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, br, "vterm_render: resize cell buffer");
        slot_count = yetty_yvterm_grid_slot_count(grid);
    }
    struct yetty_ycore_void_result rsr = vterm_ensure_row_scratch(vterm, cols);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rsr, "vterm_render: row scratch");

    /* The cell buffer is ring-slot-indexed; the shader maps visible row →
     * (row + root_row) % ring_rows, so upload by raw slot and pass the view's
     * top slot as root_row. */
    size_t row_bytes = (size_t)cols * YVTERM_WORDS_PER_CELL * sizeof(uint32_t);
    for (uint32_t slot = 0; slot < slot_count; ++slot) {
        if (!yetty_yvterm_grid_slot_dirty(grid, slot)) {
            continue;
        }
        const struct yetty_yvterm_text_cell *cells = yetty_yvterm_grid_slot_cells(grid, slot);
        if (!cells) {
            continue;
        }
        vterm_pack_line(vterm, cells, cols, vterm->row_scratch);
        wgpuQueueWriteBuffer(vterm->queue, vterm->cell_buffer, (uint64_t)slot * row_bytes,
                             vterm->row_scratch, row_bytes);
    }
    /* Glyph resolution above may have grown the atlas/meta — re-pull the font. */
    if (vterm->font->ops->is_dirty(vterm->font)) {
        struct yetty_ycore_void_result fr = vterm_upload_font(vterm);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, fr, "vterm_render: upload_font");
    }

    uint32_t cursor_row = 0, cursor_col = 0, cursor_visible = 0;
    yetty_yvterm_grid_cursor(grid, &cursor_row, &cursor_col, &cursor_visible);
    vterm->uniforms.grid_size[0] = (float)cols;
    vterm->uniforms.grid_size[1] = (float)rows;
    vterm->uniforms.cell_size[0] = vterm->cell_size.width;
    vterm->uniforms.cell_size[1] = vterm->cell_size.height;
    vterm->uniforms.ring_rows = slot_count;
    /* root_row is the ring slot drawn at visible row 0. Live view: the ring
     * base. Scrolled-back view: shift base back by how far the requested top row
     * (view_top_total_idx) sits behind the live top (total_scrolled), wrapping
     * over the ring so the retained history slots are addressed. */
    uint32_t root_row = base;
    if (vterm->view_top_active) {
        uint32_t live_top = yetty_yvterm_grid_scroll_origin(grid);
        if (vterm->view_top_total_idx < live_top) {
            uint32_t back = live_top - vterm->view_top_total_idx;
            if (slot_count > 0) {
                back %= slot_count;
                root_row = (base + slot_count - back) % slot_count;
            }
        }
    }
    vterm->uniforms.root_row = root_row;
    vterm->uniforms.cursor_col = cursor_col;
    vterm->uniforms.cursor_row = cursor_row;
    /* Hide the cursor while scrolled back — it belongs to the live bottom, not
     * the historical rows on screen. */
    vterm->uniforms.cursor_visible = vterm->view_top_active ? 0u : cursor_visible;
    /* Selection highlight: normalise (anchor, head) to reading order so the
     * shader's start<=end stream test is simple. */
    int sel_active = 0;
    uint32_t sa_row = 0, sa_col = 0, sh_row = 0, sh_col = 0;
    yetty_yvterm_grid_selection(grid, &sel_active, &sa_row, &sa_col, &sh_row, &sh_col);
    if (sel_active && (sa_row > sh_row || (sa_row == sh_row && sa_col > sh_col))) {
        uint32_t tr = sa_row, tc = sa_col;
        sa_row = sh_row;
        sa_col = sh_col;
        sh_row = tr;
        sh_col = tc;
    }
    vterm->uniforms.sel_active = (sel_active && !vterm->view_top_active) ? 1u : 0u;
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
    /* A figure is anchored on its TOP line but spans several rows downward. It
     * must stay drawn while ANY of its rows is on screen, so the scan starts
     * above visible row 0: an anchor that has scrolled into the history still
     * draws (at a negative pixel offset) as long as its extent reaches back into
     * the pane. The look-back bounds the tallest figure we keep alive — past it,
     * a figure whose anchor is that far up is fully off the top and need not draw.
     * Figures clip to the pane viewport, so an over-scan never bleeds upward. */
    int comp_lookback = (int)rows + YVTERM_COMPOSITE_LOOKBACK_ROWS;
    for (int row = -comp_lookback; row < (int)rows; ++row) {
        uint32_t comp_count = 0;
        /* Read composites by the SAME ring slot the text shader draws at this
         * visible row — slot = (row + root_row) % ring_rows — so figures scroll
         * in lockstep with the text in both live and scrolled-back views. Using
         * the visible-row (live base) accessor instead desyncs under scrollback. */
        uint32_t comp_slot;
        if (slot_count) {
            int wrapped = ((row + (int)root_row) % (int)slot_count + (int)slot_count) % (int)slot_count;
            comp_slot = (uint32_t)wrapped;
        } else {
            if (row < 0) {
                continue;
            }
            comp_slot = (uint32_t)row;
        }
        struct yetty_ydraw_composite *const *comps =
            yetty_yvterm_grid_slot_composites(grid, comp_slot, &comp_count);
        if (!comps || comp_count == 0) {
            continue;
        }
        float anchor_y = (float)row * vterm->cell_size.height;
        float comp_x = rect.min.x + (0.0f - vz_center_x - vz_off_x) * zoom + vz_center_x;
        float comp_y_local = (anchor_y - vz_center_y - vz_off_y) * zoom + vz_center_y;
        float comp_y = rect.min.y + comp_y_local;
        for (uint32_t ci = 0; ci < comp_count; ++ci) {
            if (!comps[ci]) {
                continue;
            }
            /* For an anchor that has scrolled above the pane top (row < 0), draw
             * only while the figure's extent still reaches back down into view;
             * once even its bottom row is gone it is fully off-screen. A figure
             * with no reported height falls back to the old behaviour (drawn only
             * while its anchor row is visible). An anchor pushed below the pane
             * bottom (possible under zoom) draws nothing visible either. */
            float fig_height = yetty_ydraw_composite_pixel_height(comps[ci]) * figure_scale;
            if (row < 0 && comp_y_local + fig_height <= 0.0f) {
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
                yetty_ycore_error_destroy(cr.error);
            }
        }
    }

    /* Rich SDF pass: the raw ydraw records (ycat PDF/SVG/markdown — SDF shapes,
     * glyphs, text runs) stored per line on the grid ring. Anchored by the same
     * root_row/slot mapping as the text, so they scroll in lockstep. Drawn after
     * the text + composites, on top. Best-effort — never fail the frame on it. */
    if (vterm->sdf_layer) {
        struct yetty_ycore_void_result sdf_res = yetty_yvterm_sdf_layer_render(
            vterm->sdf_layer, grid, target, rect, vterm->cell_size.width, vterm->cell_size.height,
            cols, rows, root_row, slot_count, zoom, vz_off_x, vz_off_y, cell_ratio);
        if (YETTY_IS_ERR(sdf_res)) {
            ywarn("vterm_render: SDF layer: %s", sdf_res.error.msg);
            yetty_ycore_error_destroy(sdf_res.error);
        }
    }

    /* Uploaded — clear model dirty now that the renderer has consumed the text
     * plane, the anchored composites, and the SDF/glyph records. */
    yetty_yvterm_grid_clear_dirty(grid);
    vterm->view_dirty = 0;
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Figure slots.
 *=========================================================================*/

/* Render the unified model into the figure's target: walk dirty lines, resolve
 * glyphs, upload, draw the grid quad. The render narrows to the inset content
 * rect (a docked HUD reserved a band) before drawing. */
[[clang::annotate("override@yvterm:vterm:yfigure:render")]]
static struct yetty_ycore_void_result vterm_render_slot(struct yetty_yclass_object *obj,
                                                        struct yetty_ydraw_target *target)
{
    struct yetty_yvterm_vterm_ptr_result vterm_res = yetty_yvterm_vterm_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, vterm_res, "vterm_render: from_obj");
    struct yetty_yvterm_vterm *vterm = vterm_res.value;

    struct rectangle_result rect_res = yetty_yfigure_figure_rect_get(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rect_res, "vterm_render: rect");
    struct yetty_ycore_rectangle rect = rect_res.value;
    rect.min.x += vterm->content_inset_left;
    rect.min.y += vterm->content_inset_top;
    rect.max.x -= vterm->content_inset_right;
    rect.max.y -= vterm->content_inset_bottom;

    return vterm_render_grid(vterm, target, rect);
}

[[clang::annotate("override@yvterm:vterm:yfigure:destroy")]]
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

/* Create the terminal-content figure for one pane. The rect is set in pixels;
 * cell_size is stored so the terminal can read it immediately after create. The
 * terminal hooks (pty write, render request, mouse subscription) are stored for
 * keyboard output and mouse-mode reporting.
 *
 * cell_size starts at a placeholder until the renderer build-out resolves exact
 * metrics from the active font; the terminal overwrites it on the first resize.
 * `context` is accepted now for that future font setup. */
[[clang::annotate("expose")]]
struct yetty_yclass_object_ptr_result yetty_yvterm_vterm_figure_create(
    uint32_t cols, uint32_t rows, const struct yetty_context *context,
    yetty_yterminal_pty_write_fn pty_write_fn, void *pty_write_userdata,
    yetty_yterminal_request_render_fn request_render_fn, void *request_render_userdata,
    yetty_yterminal_mouse_sub_fn mouse_sub_fn, void *mouse_sub_userdata)
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

    /* The model is a separate yvterm:grid object this figure composes/owns. */
    struct yetty_yclass_object_ptr_result grid_res = yetty_yvterm_grid_make(cols, rows);
    if (YETTY_IS_ERR(grid_res)) {
        struct yetty_ycore_void_result free_res = yetty_yclass_object_free(obj);
        if (YETTY_IS_ERR(free_res)) {
            yetty_ycore_error_destroy(free_res.error);
        }
        return YETTY_ERR(yetty_yclass_object_ptr, "yvterm vterm_create: grid_make", grid_res);
    }
    vterm->grid_obj = grid_res.value;
    /* The grid produces keyboard/query output; hand it the PTY hook. The
     * terminal's pty_write_fn matches the grid's pty_write signature. */
    yetty_yvterm_grid_set_pty_write(vterm->grid_obj, (yetty_yvterm_grid_pty_write_fn)pty_write_fn,
                                    pty_write_userdata);
    vterm->grid_size = (struct yetty_ycore_grid_size){.cols = cols, .rows = rows};
    vterm->cell_size = (struct yetty_ycore_pixel_size){.width = 9.0f, .height = 18.0f};
    vterm->request_render_fn = request_render_fn;
    vterm->request_render_userdata = request_render_userdata;
    vterm->mouse_sub_fn = mouse_sub_fn;
    vterm->mouse_sub_userdata = mouse_sub_userdata;
    vterm->visual_zoom_scale = 1.0f;

    /* Best-effort: builds the pipeline/font and overrides cell_size with the
     * font's real metrics. On failure the figure still exists (blank pane). */
    vterm_gpu_init(vterm, context);

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
[[clang::annotate("expose")]]
struct yetty_yfigure_figure *yetty_yvterm_vterm_as_figure(struct yetty_yclass_object *obj)
{
    if (!obj) {
        return NULL;
    }
    struct yetty_yfigure_figure_ptr_result figure_res = yetty_yfigure_figure_from(obj);
    if (YETTY_IS_ERR(figure_res)) {
        yetty_ycore_error_destroy(figure_res.error);
        return NULL;
    }
    return figure_res.value;
}

[[clang::annotate("expose")]] [[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_yvterm_vterm_feed(struct yetty_yclass_object *obj,
                                                       const char *bytes, size_t len)
{
    struct yetty_yvterm_vterm *vterm = vterm_body_or_null(obj);
    if (!vterm) {
        return YETTY_ERR(yetty_ycore_void, "yvterm vterm_feed: bad obj");
    }
    return yetty_yvterm_grid_feed(vterm->grid_obj, bytes, len);
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_yvterm_vterm_resize(struct yetty_yclass_object *obj,
                                                         struct yetty_ycore_grid_size grid_size,
                                                         struct yetty_ycore_pixel_size cell_size)
{
    struct yetty_yvterm_vterm *vterm = vterm_body_or_null(obj);
    if (!vterm) {
        return YETTY_ERR(yetty_ycore_void, "yvterm vterm_resize: bad obj");
    }
    if (grid_size.cols == 0 || grid_size.rows == 0) {
        return YETTY_ERR(yetty_ycore_void, "yvterm vterm_resize: invalid dimensions");
    }
    struct yetty_ycore_void_result resize_res =
        yetty_yvterm_grid_resize(vterm->grid_obj, grid_size.cols, grid_size.rows);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, resize_res, "yvterm vterm_resize: grid_resize");
    /* Reflow re-bases the grid's absolute row numbering, so any scrolled-back
     * view anchored on the old numbering is stale — snap back to the live tail. */
    vterm->view_top_active = 0;
    vterm->view_top_total_idx = 0;
    vterm->grid_size = grid_size;
    vterm->cell_size = cell_size;
    if (vterm->baseline_cell_height <= 0.0f) {
        vterm->baseline_cell_height = cell_size.height;
    }
    /* Re-scale the glyphs to the new cell. Intrusive (Ctrl+Shift) zoom changes
     * the cell stride and reflows; without this the cells grow but the font
     * stays the same size. set_cell_size re-derives the MSDF render scale (no
     * re-raster — MSDF is resolution-independent); the next frame's font upload
     * picks it up. Best-effort: a font that can't rescale keeps the old size. */
    if (vterm->font && vterm->font->ops && vterm->font->ops->set_cell_size) {
        struct yetty_ycore_void_result fcr =
            vterm->font->ops->set_cell_size(vterm->font, cell_size);
        if (YETTY_IS_ERR(fcr)) {
            yetty_ycore_error_destroy(fcr.error);
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

[[clang::annotate("expose")]]
struct yetty_ycore_pixel_size yetty_yvterm_vterm_cell_size(struct yetty_yclass_object *obj)
{
    struct yetty_yvterm_vterm *vterm = vterm_body_or_null(obj);
    if (!vterm) {
        return (struct yetty_ycore_pixel_size){0};
    }
    return vterm->cell_size;
}

[[clang::annotate("expose")]]
int yetty_yvterm_vterm_is_dirty(struct yetty_yclass_object *obj)
{
    struct yetty_yvterm_vterm *vterm = vterm_body_or_null(obj);
    if (!vterm) {
        return 0;
    }
    return vterm->view_dirty || yetty_yvterm_grid_is_dirty(vterm->grid_obj);
}

[[clang::annotate("expose")]]
void yetty_yvterm_vterm_set_content_inset(struct yetty_yclass_object *obj, float top, float right,
                                          float bottom, float left)
{
    struct yetty_yvterm_vterm *vterm = vterm_body_or_null(obj);
    if (!vterm) {
        return;
    }
    vterm->content_inset_top = top;
    vterm->content_inset_right = right;
    vterm->content_inset_bottom = bottom;
    vterm->content_inset_left = left;
}

[[clang::annotate("expose")]]
void yetty_yvterm_vterm_get_content_inset(struct yetty_yclass_object *obj, float *out_top,
                                          float *out_right, float *out_bottom, float *out_left)
{
    struct yetty_yvterm_vterm *vterm = vterm_body_or_null(obj);
    if (out_top) {
        *out_top = vterm ? vterm->content_inset_top : 0.0f;
    }
    if (out_right) {
        *out_right = vterm ? vterm->content_inset_right : 0.0f;
    }
    if (out_bottom) {
        *out_bottom = vterm ? vterm->content_inset_bottom : 0.0f;
    }
    if (out_left) {
        *out_left = vterm ? vterm->content_inset_left : 0.0f;
    }
}

[[clang::annotate("expose")]]
void yetty_yvterm_vterm_set_clear_hook(struct yetty_yclass_object *obj,
                                       yetty_yvterm_clear_hook_fn fn, void *userdata)
{
    struct yetty_yvterm_vterm *vterm = vterm_body_or_null(obj);
    if (!vterm) {
        return;
    }
    yetty_yvterm_grid_set_clear_hook(vterm->grid_obj, (yetty_yvterm_grid_clear_hook_fn)fn,
                                     userdata);
}

[[clang::annotate("expose")]]
void yetty_yvterm_vterm_cursor(struct yetty_yclass_object *obj, uint32_t *out_row,
                               uint32_t *out_col, uint32_t *out_visible)
{
    struct yetty_yvterm_vterm *vterm = vterm_body_or_null(obj);
    if (!vterm) {
        if (out_row) {
            *out_row = 0u;
        }
        if (out_col) {
            *out_col = 0u;
        }
        if (out_visible) {
            *out_visible = 0u;
        }
        return;
    }
    yetty_yvterm_grid_cursor(vterm->grid_obj, out_row, out_col, out_visible);
}

[[clang::annotate("expose")]]
void yetty_yvterm_vterm_word_bounds(struct yetty_yclass_object *obj, uint32_t row, uint32_t col,
                                    uint32_t *out_start_col, uint32_t *out_end_col)
{
    struct yetty_yvterm_vterm *vterm = vterm_body_or_null(obj);
    if (!vterm) {
        if (out_start_col) {
            *out_start_col = col;
        }
        if (out_end_col) {
            *out_end_col = col;
        }
        return;
    }
    yetty_yvterm_grid_word_bounds(vterm->grid_obj, row, col, out_start_col, out_end_col);
}

[[clang::annotate("expose")]]
uint32_t yetty_yvterm_vterm_scroll_origin(struct yetty_yclass_object *obj)
{
    struct yetty_yvterm_vterm *vterm = vterm_body_or_null(obj);
    return vterm ? yetty_yvterm_grid_scroll_origin(vterm->grid_obj) : 0u;
}

/*===========================================================================
 * Rich-content insertion — delegated to the grid model.
 *=========================================================================*/

[[clang::annotate("expose")]]
struct yetty_ycore_uint32_result yetty_yvterm_vterm_append_primitive(
    struct yetty_yclass_object *obj, uint32_t row, const uint32_t *words, uint32_t word_count)
{
    struct yetty_yvterm_vterm *vterm = vterm_body_or_null(obj);
    if (!vterm) {
        return YETTY_ERR(yetty_ycore_uint32, "yvterm append_primitive: bad obj");
    }
    return yetty_yvterm_grid_append_primitive(vterm->grid_obj, row, words, word_count);
}

[[clang::annotate("expose")]]
struct yetty_ycore_uint32_result yetty_yvterm_vterm_attach_composite(
    struct yetty_yclass_object *obj, uint32_t row, struct yetty_ydraw_composite *composite)
{
    struct yetty_yvterm_vterm *vterm = vterm_body_or_null(obj);
    if (!vterm) {
        return YETTY_ERR(yetty_ycore_uint32, "yvterm attach_composite: bad obj");
    }
    return yetty_yvterm_grid_attach_composite(vterm->grid_obj, row, composite);
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_yvterm_vterm_clear_rich_line(struct yetty_yclass_object *obj,
                                                                  uint32_t row)
{
    struct yetty_yvterm_vterm *vterm = vterm_body_or_null(obj);
    if (!vterm) {
        return YETTY_ERR(yetty_ycore_void, "yvterm clear_rich_line: bad obj");
    }
    return yetty_yvterm_grid_clear_rich_line(vterm->grid_obj, row);
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_yvterm_vterm_clear_rich_all(struct yetty_yclass_object *obj)
{
    struct yetty_yvterm_vterm *vterm = vterm_body_or_null(obj);
    if (!vterm) {
        return YETTY_ERR(yetty_ycore_void, "yvterm clear_rich_all: bad obj");
    }
    return yetty_yvterm_grid_clear_rich_all(vterm->grid_obj);
}

/*===========================================================================
 * Input, wire, selection, scrollback, zoom — delegated to the grid model
 * (except the renderer-local view/zoom state).
 *=========================================================================*/

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_yvterm_vterm_register_wire(
    struct yetty_yclass_object *obj, struct yetty_ywire_wire_statemachine *sm)
{
    struct yetty_yvterm_vterm *vterm = vterm_body_or_null(obj);
    if (!vterm) {
        return YETTY_ERR(yetty_ycore_void, "yvterm register_wire: bad obj");
    }
    return yetty_yvterm_grid_register_wire(vterm->grid_obj, sm);
}

[[clang::annotate("expose")]]
int yetty_yvterm_vterm_on_char(struct yetty_yclass_object *obj, uint32_t codepoint, int mods)
{
    struct yetty_yvterm_vterm *vterm = vterm_body_or_null(obj);
    return vterm ? yetty_yvterm_grid_on_char(vterm->grid_obj, codepoint, mods) : 0;
}

[[clang::annotate("expose")]]
int yetty_yvterm_vterm_on_key(struct yetty_yclass_object *obj, int key, int mods)
{
    struct yetty_yvterm_vterm *vterm = vterm_body_or_null(obj);
    return vterm ? yetty_yvterm_grid_on_key(vterm->grid_obj, key, mods) : 0;
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_yvterm_vterm_set_selection(struct yetty_yclass_object *obj,
                                                                int active, uint32_t anchor_row,
                                                                uint32_t anchor_col,
                                                                uint32_t head_row,
                                                                uint32_t head_col)
{
    struct yetty_yvterm_vterm *vterm = vterm_body_or_null(obj);
    if (!vterm) {
        return YETTY_ERR(yetty_ycore_void, "yvterm set_selection: bad obj");
    }
    return yetty_yvterm_grid_set_selection(vterm->grid_obj, active, anchor_row, anchor_col,
                                           head_row, head_col);
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_yvterm_vterm_get_selection_text(
    struct yetty_yclass_object *obj, struct yetty_ycore_buffer *out)
{
    struct yetty_yvterm_vterm *vterm = vterm_body_or_null(obj);
    if (!vterm) {
        return YETTY_ERR(yetty_ycore_void, "yvterm get_selection_text: bad obj");
    }
    return yetty_yvterm_grid_get_selection_text(vterm->grid_obj, out);
}

/* Scrollback is not yet backed by a history ring; the live screen is all there
 * is, so the anchor and floor are both 0 (no wheel-up range). */
/* Absolute index of the line at the live screen top: everything scrolled off so
 * far. A wheel-up anchors one line below this and walks toward the floor. */
[[clang::annotate("expose")]]
uint32_t yetty_yvterm_vterm_get_live_anchor(struct yetty_yclass_object *obj)
{
    struct yetty_yvterm_vterm *vterm = vterm_body_or_null(obj);
    if (!vterm || !vterm->grid_obj) {
        return 0u;
    }
    return yetty_yvterm_grid_scroll_origin(vterm->grid_obj);
}

/* Oldest absolute line index still retained in the scrollback ring. Lines below
 * this have been evicted, so a wheel-up clamps here. The ring keeps
 * (slot_count - visible_rows) history lines. */
[[clang::annotate("expose")]]
uint32_t yetty_yvterm_vterm_get_scrollback_floor(struct yetty_yclass_object *obj)
{
    struct yetty_yvterm_vterm *vterm = vterm_body_or_null(obj);
    if (!vterm || !vterm->grid_obj) {
        return 0u;
    }
    uint32_t live = yetty_yvterm_grid_scroll_origin(vterm->grid_obj);
    uint32_t cols = 0, rows = 0;
    yetty_yvterm_grid_dims(vterm->grid_obj, &cols, &rows, NULL);
    uint32_t ring = yetty_yvterm_grid_slot_count(vterm->grid_obj);
    uint32_t history_cap = ring > rows ? ring - rows : 0u;
    return live > history_cap ? live - history_cap : 0u;
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_yvterm_vterm_set_view_top(struct yetty_yclass_object *obj,
                                                               int active,
                                                               uint32_t view_top_total_idx)
{
    struct yetty_yvterm_vterm *vterm = vterm_body_or_null(obj);
    if (!vterm) {
        return YETTY_ERR(yetty_ycore_void, "yvterm set_view_top: bad obj");
    }
    vterm->view_top_active = active;
    vterm->view_top_total_idx = view_top_total_idx;
    vterm->view_dirty = 1;
    struct yetty_ycore_void_result dr = yetty_yfigure_figure_dirty_set(obj, 1);
    if (YETTY_IS_ERR(dr)) {
        yetty_ycore_error_destroy(dr.error);
    }
    return YETTY_OK_VOID();
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_yvterm_vterm_set_visual_zoom(struct yetty_yclass_object *obj,
                                                                  float scale, float offset_x,
                                                                  float offset_y)
{
    struct yetty_yvterm_vterm *vterm = vterm_body_or_null(obj);
    if (!vterm) {
        return YETTY_ERR(yetty_ycore_void, "yvterm set_visual_zoom: bad obj");
    }
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

#include "vterm.gen.c"
