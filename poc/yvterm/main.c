/*
 * poc/yvterm/main.c — standalone text-grid performance probe (raw WebGPU).
 *
 * Boots a window + WebGPU device via yinit_run + yframework_create (the same
 * path tools/ymaze uses), spawns $SHELL on a PTY, drives a poc_yvterm_grid
 * from the libvterm state layer, and renders it with hand-written wgpu* calls:
 * one pinned storage buffer for all cells, per-line writeBuffer uploads, and a
 * single full-screen-quad draw per frame indexing that buffer in the fragment
 * shader. No yrender binder / resource-set / render-target. The MSDF/cdb font's
 * CPU data (atlas pixels + glyph meta) is pulled from its resource set and
 * uploaded into our own texture + storage buffer.
 *
 * Keys: Ctrl-Q quits; everything else is forwarded to the child PTY.
 *
 * CLI:
 *   --stress        every frame mark all lines dirty (max upload pressure)
 *   --rows N        total line objects backing the buffer (>= visible rows)
 *   --fps N         render cap in Hz (default 30; 0 = uncapped)
 *   --cmd "<cmd>"   run <cmd> via $SHELL -c instead of an interactive shell
 */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pty.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <webgpu/webgpu.h>

#include <yetty/yconfig/config.h>
#include <yetty/ycore/result.h>
#include <yetty/yevent/event.h>
#include <yetty/yfont/ms-msdf-font.h>
#include <yetty/yframework/yframework.h>
#include <yetty/yinit/yinit.h>
#include <yetty/yclass/class.h>
#include <yetty/yplatform/extract-assets.h>
#include <yetty/yplatform/platform-input-pipe.h>
#include <yetty/yplatform/time.h>
#include <yetty/yplatform/window-manager.h>
#include <yetty/yrender/gpu-resource-set.h>
#include <yetty/yrender/render-target.h>
#include <yetty/yetty/yetty.h>

#include "grid.h"
#include "shader.h"

/* Must match the Uniforms struct in text.wgsl: grid_size vec2, cell_size vec2,
 * scale/baseline_y/glyph_left/pixel_range f32, root_row u32 + 3 u32 pad. */
struct poc_uniforms {
    float grid_size[2];
    float cell_size[2];
    float scale;
    float baseline_y;
    float glyph_left;
    float pixel_range;
    uint32_t root_row;
    uint32_t pad0;
    uint32_t pad1;
    uint32_t pad2;
};

struct poc_app {
    int quit;
    int stress;
    double target_fps;       /* render cap (Hz); 0 or --stress = uncapped */
    uint32_t requested_rows; /* --rows N; 0 = use visible rows */
    const char *child_cmd;   /* NULL → interactive $SHELL */

    struct yetty_yframework *framework;
    struct yetty_yfont_ms_font *font;
    struct poc_yvterm_grid *grid;

    /* Borrowed GPU handles from the framework. */
    WGPUDevice device;
    WGPUQueue queue;
    WGPUSurface surface;
    WGPUTextureFormat surface_format;

    /* Owned raw-wgpu resources. */
    WGPUShaderModule shader_module;
    WGPUBindGroupLayout bind_group_layout;
    WGPURenderPipeline pipeline;
    WGPUSampler sampler;
    WGPUBuffer cell_buffer;    /* one pinned STORAGE buffer for all cells */
    WGPUBuffer meta_buffer;    /* per-glyph MSDF meta */
    WGPUBuffer uniform_buffer; /* struct poc_uniforms */
    WGPUTexture atlas_texture;
    WGPUTextureView atlas_view;
    uint32_t atlas_width;
    uint32_t atlas_height;
    size_t meta_capacity; /* current meta_buffer size in bytes */
    WGPUBindGroup bind_group;
    int bind_group_valid;

    struct poc_uniforms uniforms;

    int pty_master;
    uint32_t cols;
    uint32_t visible_rows;
    uint32_t total_rows;

    /* Per-interval perf counters. */
    uint64_t lines_uploaded; /* per-line writeBuffer calls in the interval */
};

/*===========================================================================
 * PTY
 *=========================================================================*/

static struct yetty_ycore_int_result spawn_pty(struct poc_app *app)
{
    struct winsize ws = {
        .ws_row = (unsigned short)app->visible_rows,
        .ws_col = (unsigned short)app->cols,
        .ws_xpixel = 0,
        .ws_ypixel = 0,
    };
    int master = -1;
    pid_t pid = forkpty(&master, NULL, NULL, &ws);
    if (pid < 0) {
        return YETTY_ERR(yetty_ycore_int, "spawn_pty: forkpty failed");
    }
    if (pid == 0) {
        setenv("TERM", "xterm-256color", 1);
        const char *shell = getenv("SHELL");
        if (!shell || !shell[0]) {
            shell = "/bin/sh";
        }
        if (app->child_cmd) {
            execlp(shell, shell, "-c", app->child_cmd, (char *)NULL);
        } else {
            execlp(shell, shell, "-i", (char *)NULL);
        }
        _exit(127);
    }

    /* Parent: non-blocking master so the poll loop never stalls. */
    int flags = fcntl(master, F_GETFL, 0);
    fcntl(master, F_SETFL, flags | O_NONBLOCK);
    return YETTY_OK(yetty_ycore_int, master);
}

/* Forward one input event to the PTY as bytes. Ctrl-Q is intercepted as quit. */
static void forward_event_to_pty(struct poc_app *app, const struct yetty_yui_event *ev)
{
    if (ev->type == YETTY_YCORE_CHAR) {
        uint32_t codepoint = ev->chr.codepoint;
        uint8_t utf8[4];
        size_t len = 0;
        if (codepoint < 0x80) {
            utf8[len++] = (uint8_t)codepoint;
        } else if (codepoint < 0x800) {
            utf8[len++] = (uint8_t)(0xC0 | (codepoint >> 6));
            utf8[len++] = (uint8_t)(0x80 | (codepoint & 0x3F));
        } else if (codepoint < 0x10000) {
            utf8[len++] = (uint8_t)(0xE0 | (codepoint >> 12));
            utf8[len++] = (uint8_t)(0x80 | ((codepoint >> 6) & 0x3F));
            utf8[len++] = (uint8_t)(0x80 | (codepoint & 0x3F));
        } else {
            utf8[len++] = (uint8_t)(0xF0 | (codepoint >> 18));
            utf8[len++] = (uint8_t)(0x80 | ((codepoint >> 12) & 0x3F));
            utf8[len++] = (uint8_t)(0x80 | ((codepoint >> 6) & 0x3F));
            utf8[len++] = (uint8_t)(0x80 | (codepoint & 0x3F));
        }
        ssize_t wrote = write(app->pty_master, utf8, len);
        (void)wrote;
        return;
    }

    if (ev->type != YETTY_YCORE_KEY_DOWN) {
        return;
    }

    int key = ev->key.key;
    int ctrl = (ev->key.mods & 2) != 0; /* GLFW_MOD_CONTROL */
    if (ctrl && (key == 'Q' || key == 'q')) {
        app->quit = 1;
        return;
    }

    char byte = 0;
    switch (key) {
    case 257: /* Enter */
        byte = '\r';
        break;
    case 258: /* Tab */
        byte = '\t';
        break;
    case 259: /* Backspace */
        byte = 0x7f;
        break;
    case 256: /* Escape */
        byte = 0x1b;
        break;
    default:
        if (ctrl && key >= 'A' && key <= 'Z') {
            byte = (char)(key & 0x1f); /* Ctrl-letter control code */
        }
        break;
    }
    if (byte) {
        ssize_t wrote = write(app->pty_master, &byte, 1);
        (void)wrote;
    }
}

/*===========================================================================
 * GPU setup — raw wgpu*
 *=========================================================================*/

static WGPUStringView sv(const char *text)
{
    WGPUStringView view = {.data = text, .length = strlen(text)};
    return view;
}

static struct yetty_ycore_void_result create_pipeline(struct poc_app *app)
{
    WGPUShaderSourceWGSL wgsl = {0};
    wgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgsl.code = sv(poc_yvterm_text_wgsl);

    WGPUShaderModuleDescriptor shader_desc = {0};
    shader_desc.nextInChain = &wgsl.chain;
    shader_desc.label = sv("poc-yvterm text shader");
    app->shader_module = wgpuDeviceCreateShaderModule(app->device, &shader_desc);
    if (!app->shader_module) {
        return YETTY_ERR(yetty_ycore_void, "create_pipeline: shader module");
    }

    /* Bind group layout:
     *   0 storage read-only (cells)
     *   1 storage read-only (meta)
     *   2 texture_2d float (atlas)
     *   3 filtering sampler
     *   4 uniform buffer */
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
    entries[4].buffer.minBindingSize = sizeof(struct poc_uniforms);

    WGPUBindGroupLayoutDescriptor bgl_desc = {0};
    bgl_desc.entryCount = 5;
    bgl_desc.entries = entries;
    app->bind_group_layout = wgpuDeviceCreateBindGroupLayout(app->device, &bgl_desc);
    if (!app->bind_group_layout) {
        return YETTY_ERR(yetty_ycore_void, "create_pipeline: bind group layout");
    }

    WGPUPipelineLayoutDescriptor pl_desc = {0};
    pl_desc.bindGroupLayoutCount = 1;
    pl_desc.bindGroupLayouts = &app->bind_group_layout;
    WGPUPipelineLayout layout = wgpuDeviceCreatePipelineLayout(app->device, &pl_desc);
    if (!layout) {
        return YETTY_ERR(yetty_ycore_void, "create_pipeline: pipeline layout");
    }

    WGPUColorTargetState color_target = {0};
    color_target.format = app->surface_format;
    color_target.writeMask = WGPUColorWriteMask_All; /* opaque, no blend */

    WGPUFragmentState fragment = {0};
    fragment.module = app->shader_module;
    fragment.entryPoint = sv("fs_main");
    fragment.targetCount = 1;
    fragment.targets = &color_target;

    WGPURenderPipelineDescriptor rp_desc = {0};
    rp_desc.label = sv("poc-yvterm pipeline");
    rp_desc.layout = layout;
    rp_desc.vertex.module = app->shader_module;
    rp_desc.vertex.entryPoint = sv("vs_main");
    rp_desc.fragment = &fragment;
    rp_desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    rp_desc.primitive.frontFace = WGPUFrontFace_CCW;
    rp_desc.primitive.cullMode = WGPUCullMode_None;
    rp_desc.multisample.count = 1;
    rp_desc.multisample.mask = ~0u;

    app->pipeline = wgpuDeviceCreateRenderPipeline(app->device, &rp_desc);
    wgpuPipelineLayoutRelease(layout);
    if (!app->pipeline) {
        return YETTY_ERR(yetty_ycore_void, "create_pipeline: render pipeline");
    }

    WGPUSamplerDescriptor sampler_desc = {0};
    sampler_desc.minFilter = WGPUFilterMode_Linear;
    sampler_desc.magFilter = WGPUFilterMode_Linear;
    sampler_desc.addressModeU = WGPUAddressMode_ClampToEdge;
    sampler_desc.addressModeV = WGPUAddressMode_ClampToEdge;
    sampler_desc.maxAnisotropy = 1;
    app->sampler = wgpuDeviceCreateSampler(app->device, &sampler_desc);
    if (!app->sampler) {
        return YETTY_ERR(yetty_ycore_void, "create_pipeline: sampler");
    }

    return YETTY_OK_VOID();
}

/* The one pinned cell buffer: total_rows * cols * 16 bytes, created once. */
static struct yetty_ycore_void_result create_cell_buffer(struct poc_app *app)
{
    uint64_t size = (uint64_t)app->total_rows * app->cols * POC_YVTERM_WORDS_PER_CELL *
                    sizeof(uint32_t);
    WGPUBufferDescriptor desc = {0};
    desc.label = sv("poc-yvterm cells");
    desc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
    desc.size = size;
    app->cell_buffer = wgpuDeviceCreateBuffer(app->device, &desc);
    if (!app->cell_buffer) {
        return YETTY_ERR(yetty_ycore_void, "create_cell_buffer: buffer");
    }

    WGPUBufferDescriptor uni_desc = {0};
    uni_desc.label = sv("poc-yvterm uniforms");
    uni_desc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    uni_desc.size = sizeof(struct poc_uniforms);
    app->uniform_buffer = wgpuDeviceCreateBuffer(app->device, &uni_desc);
    if (!app->uniform_buffer) {
        return YETTY_ERR(yetty_ycore_void, "create_cell_buffer: uniform buffer");
    }
    return YETTY_OK_VOID();
}

/* Recreate the atlas texture at a new size. Invalidates the bind group. */
static struct yetty_ycore_void_result recreate_atlas(struct poc_app *app, uint32_t width,
                                                     uint32_t height)
{
    if (app->atlas_view) {
        wgpuTextureViewRelease(app->atlas_view);
        app->atlas_view = NULL;
    }
    if (app->atlas_texture) {
        wgpuTextureDestroy(app->atlas_texture);
        wgpuTextureRelease(app->atlas_texture);
        app->atlas_texture = NULL;
    }

    WGPUTextureDescriptor tex_desc = {0};
    tex_desc.label = sv("poc-yvterm atlas");
    tex_desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    tex_desc.dimension = WGPUTextureDimension_2D;
    tex_desc.size.width = width;
    tex_desc.size.height = height;
    tex_desc.size.depthOrArrayLayers = 1;
    tex_desc.format = WGPUTextureFormat_RGBA8Unorm;
    tex_desc.mipLevelCount = 1;
    tex_desc.sampleCount = 1;
    app->atlas_texture = wgpuDeviceCreateTexture(app->device, &tex_desc);
    if (!app->atlas_texture) {
        return YETTY_ERR(yetty_ycore_void, "recreate_atlas: texture");
    }
    app->atlas_view = wgpuTextureCreateView(app->atlas_texture, NULL);
    if (!app->atlas_view) {
        return YETTY_ERR(yetty_ycore_void, "recreate_atlas: view");
    }
    app->atlas_width = width;
    app->atlas_height = height;
    app->bind_group_valid = 0;
    return YETTY_OK_VOID();
}

/* Recreate the meta buffer at a new size. Invalidates the bind group. */
static struct yetty_ycore_void_result recreate_meta_buffer(struct poc_app *app, size_t size)
{
    if (app->meta_buffer) {
        wgpuBufferDestroy(app->meta_buffer);
        wgpuBufferRelease(app->meta_buffer);
        app->meta_buffer = NULL;
    }
    WGPUBufferDescriptor desc = {0};
    desc.label = sv("poc-yvterm meta");
    desc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
    desc.size = size;
    app->meta_buffer = wgpuDeviceCreateBuffer(app->device, &desc);
    if (!app->meta_buffer) {
        return YETTY_ERR(yetty_ycore_void, "recreate_meta_buffer: buffer");
    }
    app->meta_capacity = size;
    app->bind_group_valid = 0;
    return YETTY_OK_VOID();
}

/* Pull the font's CPU resource set and upload atlas pixels + glyph meta into
 * our own GPU resources. Recreates atlas/meta when they grow. */
static struct yetty_ycore_void_result upload_font(struct poc_app *app)
{
    struct yetty_yrender_gpu_resource_set_result rs_res =
        app->font->ops->get_gpu_resource_set(app->font);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rs_res, "upload_font: get_gpu_resource_set");
    const struct yetty_yrender_gpu_resource_set *rs = rs_res.value;

    /* Uniforms [0..3]: pixel_range, scale, baseline_y, glyph_left. */
    app->uniforms.pixel_range = rs->uniforms[0].f32;
    app->uniforms.scale = rs->uniforms[1].f32;
    app->uniforms.baseline_y = rs->uniforms[2].f32;
    app->uniforms.glyph_left = rs->uniforms[3].f32;

    /* Atlas texture (textures[0]: RGBA8 pixels). */
    const struct yetty_yrender_texture *atlas = &rs->textures[0];
    if (atlas->data && atlas->width > 0 && atlas->height > 0) {
        if (!app->atlas_texture || atlas->width != app->atlas_width ||
            atlas->height != app->atlas_height) {
            struct yetty_ycore_void_result re = recreate_atlas(app, atlas->width, atlas->height);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, re, "upload_font: recreate_atlas");
        }
        WGPUTexelCopyTextureInfo dest = {0};
        dest.texture = app->atlas_texture;
        dest.mipLevel = 0;
        WGPUTexelCopyBufferLayout src_layout = {0};
        src_layout.bytesPerRow = atlas->width * 4u;
        src_layout.rowsPerImage = atlas->height;
        WGPUExtent3D extent = {atlas->width, atlas->height, 1};
        size_t bytes = (size_t)atlas->width * atlas->height * 4u;
        wgpuQueueWriteTexture(app->queue, &dest, atlas->data, bytes, &src_layout, &extent);
    }

    /* Glyph meta (buffers[0]: 10 floats per glyph). */
    const struct yetty_yrender_buffer *meta = &rs->buffers[0];
    if (meta->data && meta->size > 0) {
        if (!app->meta_buffer || meta->size > app->meta_capacity) {
            struct yetty_ycore_void_result re = recreate_meta_buffer(app, meta->size);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, re, "upload_font: recreate_meta_buffer");
        }
        wgpuQueueWriteBuffer(app->queue, app->meta_buffer, 0, meta->data, meta->size);
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result ensure_bind_group(struct poc_app *app)
{
    if (app->bind_group_valid && app->bind_group) {
        return YETTY_OK_VOID();
    }
    if (app->bind_group) {
        wgpuBindGroupRelease(app->bind_group);
        app->bind_group = NULL;
    }
    if (!app->cell_buffer || !app->meta_buffer || !app->atlas_view || !app->sampler ||
        !app->uniform_buffer) {
        return YETTY_ERR(yetty_ycore_void, "ensure_bind_group: missing resource");
    }

    WGPUBindGroupEntry entries[5] = {0};
    entries[0].binding = 0;
    entries[0].buffer = app->cell_buffer;
    entries[0].size = WGPU_WHOLE_SIZE;
    entries[1].binding = 1;
    entries[1].buffer = app->meta_buffer;
    entries[1].size = WGPU_WHOLE_SIZE;
    entries[2].binding = 2;
    entries[2].textureView = app->atlas_view;
    entries[3].binding = 3;
    entries[3].sampler = app->sampler;
    entries[4].binding = 4;
    entries[4].buffer = app->uniform_buffer;
    entries[4].size = sizeof(struct poc_uniforms);

    WGPUBindGroupDescriptor bg_desc = {0};
    bg_desc.layout = app->bind_group_layout;
    bg_desc.entryCount = 5;
    bg_desc.entries = entries;
    app->bind_group = wgpuDeviceCreateBindGroup(app->device, &bg_desc);
    if (!app->bind_group) {
        return YETTY_ERR(yetty_ycore_void, "ensure_bind_group: create bind group");
    }
    app->bind_group_valid = 1;
    return YETTY_OK_VOID();
}

/* Re-create the one pinned cell buffer at the current total_rows x cols and
 * invalidate the bind group (which references it). The uniform buffer is fixed
 * size and is left alone. */
static struct yetty_ycore_void_result resize_cell_buffer(struct poc_app *app)
{
    if (app->cell_buffer) {
        wgpuBufferDestroy(app->cell_buffer);
        wgpuBufferRelease(app->cell_buffer);
        app->cell_buffer = NULL;
    }
    uint64_t size =
        (uint64_t)app->total_rows * app->cols * POC_YVTERM_WORDS_PER_CELL * sizeof(uint32_t);
    WGPUBufferDescriptor desc = {0};
    desc.label = sv("poc-yvterm cells");
    desc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
    desc.size = size;
    app->cell_buffer = wgpuDeviceCreateBuffer(app->device, &desc);
    if (!app->cell_buffer) {
        return YETTY_ERR(yetty_ycore_void, "resize_cell_buffer: buffer");
    }
    app->bind_group_valid = 0;
    return YETTY_OK_VOID();
}

/* On window resize / maximize: recompute the grid so it fills the new viewport
 * at the font's cell size, reflow the vterm + line collection, grow the GPU
 * buffer, and SIGWINCH the child so it repaints at the new dimensions. */
static struct yetty_ycore_void_result handle_resize(struct poc_app *app, uint32_t width,
                                                    uint32_t height)
{
    struct yetty_ycore_void_result sr =
        yetty_yframework_reconfigure_surface(app->framework, width, height);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "handle_resize: reconfigure_surface");

    struct pixel_size_result cell = app->font->ops->get_cell_size(app->font);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, cell, "handle_resize: cell size");

    uint32_t cols = (uint32_t)((float)width / cell.value.width);
    uint32_t rows = (uint32_t)((float)height / cell.value.height);
    if (cols == 0) {
        cols = 1;
    }
    if (rows == 0) {
        rows = 1;
    }
    if (cols == app->cols && rows == app->visible_rows) {
        return YETTY_OK_VOID(); /* surface changed but the grid is unchanged */
    }
    uint32_t total_rows = (app->requested_rows > rows) ? app->requested_rows : rows;

    struct yetty_ycore_void_result gr = poc_yvterm_grid_resize(app->grid, cols, rows, total_rows);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, gr, "handle_resize: grid resize");
    app->cols = cols;
    app->visible_rows = rows;
    app->total_rows = total_rows;

    struct yetty_ycore_void_result br = resize_cell_buffer(app);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, br, "handle_resize: cell buffer");

    app->uniforms.cell_size[0] = cell.value.width;
    app->uniforms.cell_size[1] = cell.value.height;

    /* Tell the child its new size → SIGWINCH → it reflows and repaints. */
    struct winsize ws = {
        .ws_row = (unsigned short)rows,
        .ws_col = (unsigned short)cols,
        .ws_xpixel = (unsigned short)width,
        .ws_ypixel = (unsigned short)height,
    };
    ioctl(app->pty_master, TIOCSWINSZ, &ws);
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Frame
 *=========================================================================*/

static struct yetty_ycore_void_result render_frame(struct poc_app *app)
{
    /* Re-pull the font if it loaded new glyphs (atlas/meta may have grown). */
    if (app->font->ops->is_dirty(app->font)) {
        struct yetty_ycore_void_result fr = upload_font(app);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, fr, "render_frame: upload_font");
    }

    /* Upload each dirty line individually into the one pinned cell buffer. */
    struct poc_line *lines = poc_yvterm_grid_lines(app->grid);
    uint32_t cols = app->cols;
    size_t row_bytes = (size_t)cols * POC_YVTERM_WORDS_PER_CELL * sizeof(uint32_t);
    for (uint32_t line_index = 0; line_index < app->total_rows; line_index++) {
        if (!lines[line_index].dirty) {
            continue;
        }
        wgpuQueueWriteBuffer(app->queue, app->cell_buffer, (uint64_t)line_index * row_bytes,
                             lines[line_index].cells, row_bytes);
        lines[line_index].dirty = 0;
        app->lines_uploaded++;
    }
    /* All dirty lines consumed — the grid is clean until the next feed. */
    poc_yvterm_grid_clear_dirty(app->grid);

    /* Uniforms. root_row carries the rolling ring offset; the shader remaps
     * visible row -> GPU slot with (row + root_row) % rows. */
    app->uniforms.grid_size[0] = (float)cols;
    app->uniforms.grid_size[1] = (float)app->visible_rows;
    app->uniforms.root_row = poc_yvterm_grid_base(app->grid);
    wgpuQueueWriteBuffer(app->queue, app->uniform_buffer, 0, &app->uniforms,
                         sizeof(struct poc_uniforms));

    struct yetty_ycore_void_result bg = ensure_bind_group(app);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, bg, "render_frame: bind group");

    /* Acquire the swapchain texture. */
    WGPUSurfaceTexture surface_texture;
    wgpuSurfaceGetCurrentTexture(app->surface, &surface_texture);
    if (surface_texture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
        surface_texture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) {
        return YETTY_ERR(yetty_ycore_void, "render_frame: GetCurrentTexture not Success");
    }
    WGPUTextureView surface_view = wgpuTextureCreateView(surface_texture.texture, NULL);
    if (!surface_view) {
        return YETTY_ERR(yetty_ycore_void, "render_frame: surface view");
    }

    WGPUCommandEncoderDescriptor enc_desc = {0};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(app->device, &enc_desc);
    if (!encoder) {
        wgpuTextureViewRelease(surface_view);
        return YETTY_ERR(yetty_ycore_void, "render_frame: encoder");
    }

    WGPURenderPassColorAttachment color_attachment = {0};
    color_attachment.view = surface_view;
    color_attachment.loadOp = WGPULoadOp_Clear;
    color_attachment.storeOp = WGPUStoreOp_Store;
    color_attachment.clearValue = (WGPUColor){0.0, 0.0, 0.0, 1.0};
    color_attachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;

    WGPURenderPassDescriptor pass_desc = {0};
    pass_desc.colorAttachmentCount = 1;
    pass_desc.colorAttachments = &color_attachment;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &pass_desc);
    if (!pass) {
        wgpuCommandEncoderRelease(encoder);
        wgpuTextureViewRelease(surface_view);
        return YETTY_ERR(yetty_ycore_void, "render_frame: begin render pass");
    }

    /* One draw per frame: full-screen quad, positions from vertex_index. */
    wgpuRenderPassEncoderSetPipeline(pass, app->pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, app->bind_group, 0, NULL);
    wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);

    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    WGPUCommandBufferDescriptor cmd_desc = {0};
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, &cmd_desc);
    wgpuQueueSubmit(app->queue, 1, &cmd);
    wgpuCommandBufferRelease(cmd);
    wgpuCommandEncoderRelease(encoder);
    wgpuTextureViewRelease(surface_view);

    wgpuSurfacePresent(app->surface);
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Teardown
 *=========================================================================*/

static void destroy_gpu_resources(struct poc_app *app)
{
    if (app->bind_group) {
        wgpuBindGroupRelease(app->bind_group);
        app->bind_group = NULL;
    }
    if (app->atlas_view) {
        wgpuTextureViewRelease(app->atlas_view);
        app->atlas_view = NULL;
    }
    if (app->atlas_texture) {
        wgpuTextureDestroy(app->atlas_texture);
        wgpuTextureRelease(app->atlas_texture);
        app->atlas_texture = NULL;
    }
    if (app->meta_buffer) {
        wgpuBufferDestroy(app->meta_buffer);
        wgpuBufferRelease(app->meta_buffer);
        app->meta_buffer = NULL;
    }
    if (app->uniform_buffer) {
        wgpuBufferDestroy(app->uniform_buffer);
        wgpuBufferRelease(app->uniform_buffer);
        app->uniform_buffer = NULL;
    }
    if (app->cell_buffer) {
        wgpuBufferDestroy(app->cell_buffer);
        wgpuBufferRelease(app->cell_buffer);
        app->cell_buffer = NULL;
    }
    if (app->sampler) {
        wgpuSamplerRelease(app->sampler);
        app->sampler = NULL;
    }
    if (app->pipeline) {
        wgpuRenderPipelineRelease(app->pipeline);
        app->pipeline = NULL;
    }
    if (app->bind_group_layout) {
        wgpuBindGroupLayoutRelease(app->bind_group_layout);
        app->bind_group_layout = NULL;
    }
    if (app->shader_module) {
        wgpuShaderModuleRelease(app->shader_module);
        app->shader_module = NULL;
    }
}

/*===========================================================================
 * Worker
 *=========================================================================*/

static struct yetty_ycore_void_result poc_worker(struct yetty_yinit_runtime *runtime, void *user)
{
    struct poc_app *app = user;

    struct yetty_yframework_ptr_result framework_res = yetty_yframework_create(runtime);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, framework_res, "yframework_create");
    app->framework = framework_res.value;

    /* The framework's own render target is unused — we render with raw wgpu*
     * straight to the surface. Tear it down so nothing else drives the
     * surface behind our back. */
    if (app->framework->render_target) {
        app->framework->render_target->ops->destroy(app->framework->render_target);
        app->framework->render_target = NULL;
    }

    app->device = app->framework->gpu.device;
    app->queue = app->framework->gpu.queue;
    app->surface = (WGPUSurface)runtime->surface;
    app->surface_format = app->framework->gpu.surface_format;

    uint32_t surface_w = runtime->surface_width;
    uint32_t surface_h = runtime->surface_height;

    /* MSDF font from the cdb — the real slow path. */
    struct yetty_yconfig_config *config = app->framework->config;
    const char *fonts_dir = config->ops->get_string(config, "paths/fonts", "");
    const char *shaders_dir = config->ops->get_string(config, "paths/shaders", "");
    char cdb_path[512];
    char font_shader_path[512];
    snprintf(cdb_path, sizeof(cdb_path), "%s/../msdf-fonts/DejaVuSansMNerdFontMono-Regular.cdb",
             fonts_dir);
    snprintf(font_shader_path, sizeof(font_shader_path), "%s/ms-msdf-font.wgsl", shaders_dir);

    float content_scale = app->framework->gpu.app_gpu_context.content_scale;
    if (content_scale <= 0.0f) {
        content_scale = 1.0f;
    }
    /* Half the configured font size → smaller cells, denser grid: the dense
     * 4K case the probe is meant to stress. */
    float font_size = (float)config->ops->get_int(config, "terminal/text-layer/font/size", 14) *
                      content_scale * 0.5f;
    struct yetty_yfont_ms_padding padding = {0};

    struct yetty_font_ms_font_result font_res =
        yetty_yfont_ms_msdf_font_create(cdb_path, font_shader_path, font_size, padding);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, font_res, "ms_msdf_font_create");
    app->font = font_res.value;

    struct pixel_size_result cell = app->font->ops->get_cell_size(app->font);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, cell, "font get_cell_size");
    app->cols = (uint32_t)((float)surface_w / cell.value.width);
    app->visible_rows = (uint32_t)((float)surface_h / cell.value.height);
    if (app->cols == 0) {
        app->cols = 1;
    }
    if (app->visible_rows == 0) {
        app->visible_rows = 1;
    }
    app->total_rows = app->visible_rows;
    if (app->requested_rows > app->total_rows) {
        app->total_rows = app->requested_rows;
    }

    app->uniforms.cell_size[0] = cell.value.width;
    app->uniforms.cell_size[1] = cell.value.height;

    struct poc_yvterm_grid_ptr_result grid_res =
        poc_yvterm_grid_create(app->cols, app->visible_rows, app->total_rows, app->font);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, grid_res, "grid create");
    app->grid = grid_res.value;

    struct yetty_ycore_void_result pr = create_pipeline(app);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, pr, "create_pipeline");
    struct yetty_ycore_void_result cbr = create_cell_buffer(app);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, cbr, "create_cell_buffer");
    struct yetty_ycore_void_result ufr = upload_font(app);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, ufr, "upload_font");

    fprintf(stderr,
            "poc-yvterm: %ux%u visible (%ux%u total) cells (%.1fx%.1f px), surface %ux%u, "
            "stress=%d — Ctrl-Q quits\n",
            app->cols, app->visible_rows, app->cols, app->total_rows, cell.value.width,
            cell.value.height, surface_w, surface_h, app->stress);

    struct yetty_ycore_int_result pty_res = spawn_pty(app);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, pty_res, "spawn_pty");
    app->pty_master = pty_res.value;

    struct yetty_ycore_int_result pipe_fd_res =
        runtime->platform_input_pipe->ops->read_fd(runtime->platform_input_pipe);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, pipe_fd_res, "input pipe read_fd");
    int pipe_fd = pipe_fd_res.value;

    /* Perf accounting. */
    double stats_start = yetty_yplatform_ytime_monotonic_sec();
    uint64_t frames = 0;
    double frame_time_accum = 0.0;

    /* Render is driven by a software timer: at most one frame per period, and
     * only if the grid is dirty. period <= 0 means uncapped (stress / --fps 0). */
    double frame_period = (app->target_fps > 0.0 && !app->stress) ? 1.0 / app->target_fps : 0.0;
    double next_render = yetty_yplatform_ytime_monotonic_sec();

    while (!app->quit) {
        struct pollfd pfds[2] = {
            {.fd = app->pty_master, .events = POLLIN},
            {.fd = pipe_fd, .events = POLLIN},
        };
        /* Timer-paced poll: when content is pending, sleep only until the next
         * render tick; when idle, block until input (1s safety wakeup) so an
         * idle terminal uses ~no CPU. Uncapped (stress / --fps 0) spins. */
        double loop_now = yetty_yplatform_ytime_monotonic_sec();
        int timeout_ms;
        if (frame_period <= 0.0) {
            timeout_ms = 0;
        } else if (poc_yvterm_grid_is_dirty(app->grid)) {
            double wait = next_render - loop_now;
            timeout_ms = wait <= 0.0 ? 0 : (int)(wait * 1000.0) + 1;
        } else {
            timeout_ms = 1000;
        }
        int ready = poll(pfds, 2, timeout_ms);
        if (ready < 0 && errno != EINTR) {
            break;
        }

        if (pfds[0].revents & POLLIN) {
            char buf[65536];
            for (;;) {
                ssize_t got = read(app->pty_master, buf, sizeof(buf));
                if (got > 0) {
                    struct yetty_ycore_void_result fr =
                        poc_yvterm_grid_feed(app->grid, buf, (size_t)got);
                    if (YETTY_IS_ERR(fr)) {
                        yetty_ycore_error_destroy(fr.error);
                    }
                    if (got < (ssize_t)sizeof(buf)) {
                        break;
                    }
                } else if (got == 0) {
                    app->quit = 1; /* child exited */
                    break;
                } else {
                    break; /* EAGAIN */
                }
            }
        }

        if (pfds[1].revents & POLLIN) {
            for (;;) {
                struct yetty_yui_event ev = {0};
                struct yetty_ycore_size_result rr = runtime->platform_input_pipe->ops->read(
                    runtime->platform_input_pipe, &ev, sizeof(ev));
                if (YETTY_IS_ERR(rr) || rr.value != sizeof(ev)) {
                    break;
                }
                if (ev.type == YETTY_YCORE_SHUTDOWN || ev.type == YETTY_YCORE_WINDOW_CLOSE) {
                    app->quit = 1;
                } else if (ev.type == YETTY_YCORE_MOUSE_DOUBLE_CLICK) {
                    /* Double-click anywhere toggles window maximize — the only
                     * way to maximize on a 4K screen with no usable titlebar. */
                    if (app->framework->window_manager) {
                        struct yetty_yclass_ctx ctx = {0};
                        struct yetty_ycore_void_result mr =
                            yetty_yplatform_window_manager_toggle_maximize(
                                &ctx, app->framework->window_manager);
                        if (YETTY_IS_ERR(mr)) {
                            yetty_ycore_error_destroy(mr.error);
                        }
                    }
                } else if (ev.type == YETTY_YCORE_RESIZE) {
                    uint32_t width = (uint32_t)ev.resize.width;
                    uint32_t height = (uint32_t)ev.resize.height;
                    if (width && height) {
                        struct yetty_ycore_void_result rr = handle_resize(app, width, height);
                        if (YETTY_IS_ERR(rr)) {
                            yetty_ycore_error_destroy(rr.error);
                        }
                    }
                } else {
                    forward_event_to_pty(app, &ev);
                }
            }
        }

        if (app->quit) {
            break;
        }

        if (app->stress) {
            poc_yvterm_grid_force_full_dirty(app->grid);
        }

        /* Timer-driven render: at most once per frame_period, and only if the
         * grid is dirty. Every scroll / PTY write since the last tick coalesces
         * into this one frame — a smooth capped FPS, not skip-9-out-of-10. Dawn's
         * event pump runs only after a present. */
        double render_now = yetty_yplatform_ytime_monotonic_sec();
        if (poc_yvterm_grid_is_dirty(app->grid) &&
            (frame_period <= 0.0 || render_now >= next_render)) {
            struct yetty_ycore_void_result fr = render_frame(app);
            if (YETTY_IS_ERR(fr)) {
                yetty_ycore_error_destroy(fr.error);
            }
            if (runtime->instance) {
                wgpuInstanceProcessEvents((WGPUInstance)runtime->instance);
            }
            frame_time_accum += yetty_yplatform_ytime_monotonic_sec() - render_now;
            frames++;
            next_render = render_now + frame_period;
        }

        double now = yetty_yplatform_ytime_monotonic_sec();
        double elapsed = now - stats_start;
        if (elapsed >= 1.0) {
            double fps = (double)frames / elapsed;
            double avg_ms = frames ? (frame_time_accum / (double)frames) * 1000.0 : 0.0;
            double lines_per_frame = frames ? (double)app->lines_uploaded / (double)frames : 0.0;
            double uploaded_mb = (double)app->lines_uploaded *
                                 (double)app->cols * POC_YVTERM_WORDS_PER_CELL *
                                 sizeof(uint32_t) / (1024.0 * 1024.0);
            fprintf(stderr,
                    "poc-yvterm: %.0f fps | %.2f ms/frame | %llu frames | "
                    "%.0f lines/frame uploaded | %.1f MB uploaded\n",
                    fps, avg_ms, (unsigned long long)frames, lines_per_frame, uploaded_mb);
            stats_start = now;
            frames = 0;
            frame_time_accum = 0.0;
            app->lines_uploaded = 0;
        }
    }

    /* Teardown. */
    if (app->pty_master >= 0) {
        close(app->pty_master);
    }
    destroy_gpu_resources(app);
    struct yetty_ycore_void_result gd = poc_yvterm_grid_destroy(app->grid);
    if (YETTY_IS_ERR(gd)) {
        yetty_ycore_error_destroy(gd.error);
    }
    app->grid = NULL;
    if (app->font) {
        app->font->ops->destroy(app->font);
        app->font = NULL;
    }
    yetty_yframework_destroy(app->framework);
    app->framework = NULL;
    return YETTY_OK_VOID();
}

int main(int argc, char **argv)
{
    struct poc_app app = {0};
    app.pty_master = -1;
    app.target_fps = 30.0; /* default render cap */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--stress") == 0) {
            app.stress = 1;
        } else if (strcmp(argv[i], "--rows") == 0 && i + 1 < argc) {
            app.requested_rows = (uint32_t)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--fps") == 0 && i + 1 < argc) {
            app.target_fps = strtod(argv[++i], NULL); /* 0 = uncapped */
        } else if (strcmp(argv[i], "--cmd") == 0 && i + 1 < argc) {
            app.child_cmd = argv[++i];
        }
    }

    struct yetty_yinit_app_config cfg = {
        .extract_assets_fn = yetty_platform_extract_assets,
    };
    /* Our --stress/--rows/--fps/--cmd flags are not yinit's; hand yinit a clean
     * argv (just the program name) so its own option parser doesn't choke. */
    char *yinit_argv[] = {argv[0], NULL};
    struct yetty_ycore_int_result run_res = yetty_yinit_run(1, yinit_argv, &cfg, poc_worker, &app);
    if (YETTY_IS_ERR(run_res)) {
        yetty_ycore_error_print(stderr, "poc-yvterm", run_res.error);
        yetty_ycore_error_destroy(run_res.error);
        return 1;
    }
    return run_res.value;
}
