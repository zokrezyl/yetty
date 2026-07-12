/*
 * pipeline.c — shared, type-level GPU pipeline.
 *
 * Compiled ONCE from a template resource_set tree. Owns shader_module,
 * bind_group_layout, pipeline_layout, pipeline, and a shared full-screen
 * quad vertex buffer. Per-instance gpu_resource_binder objects reference
 * this pipeline by const pointer and create their own bind_groups against
 * its layout.
 *
 * Several helpers (append_shader, compute_tree_shader_hash,
 * collect_resources, generate_wgsl_bindings) are deliberately duplicated
 * from gpu-resource-binder.c — both walk a resource_set tree and emit
 * bindings, but the binder uses the result to create per-instance GPU
 * buffers + bind_group while this file uses it only to create the
 * shared pipeline layout/shader. Extracting them into a shared TU
 * crosses an internal data-struct boundary; duplicating is cheaper.
 */

#include <yetty/yrender/pipeline.h>

#include <yetty/yrender/gpu-allocator.h>
#include <yetty/webgpu/error.h>
#include <yetty/ytrace/ytrace.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_FLAT_BUFFERS 64
#define MAX_FLAT_TEXTURES 64
#define MAX_FLAT_UNIFORMS 256
#define MAX_VISITED_RS 128
#define MAX_BINDING_CODE 16384

/*=============================================================================
 * Local flat-resource view used while compiling the shader / building the
 * bind_group_layout. NO per-instance values — only the structural shape
 * (counts, types, names, atlas slots).
 *===========================================================================*/

#define ATLAS_R8 0
#define ATLAS_RGBA8 1
#define ATLAS_COUNT 2

static const char *const atlas_names[ATLAS_COUNT] = {"atlas_r8", "atlas_rgba8"};

struct flat_buffer_view {
    const struct yetty_yrender_buffer *src;
    const char *ns;
    size_t mega_offset; /* derived from template-rs sizes; equals per-instance offset */
};

struct flat_texture_view {
    const struct yetty_yrender_texture *src;
    const char *ns;
    size_t atlas_index;
};

struct flat_uniform_view {
    const struct yetty_yrender_uniform *src;
    const char *ns;
};

struct flatten_state {
    struct flat_buffer_view buffers[MAX_FLAT_BUFFERS];
    size_t buffer_count;
    size_t storage_total;

    struct flat_texture_view textures[MAX_FLAT_TEXTURES];
    size_t texture_count;
    int atlas_present[ATLAS_COUNT];

    struct flat_uniform_view uniforms[MAX_FLAT_UNIFORMS];
    size_t uniform_count;

    /* Merged shader code (children-first, parent-last) */
    char *shader_data;
    size_t shader_size;
    size_t shader_capacity;

    const struct yetty_yrender_gpu_resource_set *visited[MAX_VISITED_RS];
    size_t visited_count;
};

/* Append a shader fragment, substituting every literal occurrence of
 * "__NS__" with `ns`. This lets a shader source file (e.g. msdf-font.wgsl)
 * use `__NS___glyph_sample`, `uniforms.__NS___base_size` etc. and have the
 * binder paste in the actual instance namespace at merge time, so multiple
 * instances of the same shader type don't collide on identifier names. */
static void shader_append(struct flatten_state *st, const struct yetty_yrender_shader_code *sc,
                          const char *ns)
{
    if (!sc->data || sc->size == 0) {
        return;
    }
    size_t ns_len = ns ? strlen(ns) : 0;
    /* Worst case: every byte expands to ns_len bytes. Cheap upper bound. */
    size_t needed = st->shader_size + sc->size * (ns_len > 7 ? ns_len : 7) / 7 + 2;
    if (needed > st->shader_capacity) {
        size_t new_cap = needed * 2;
        st->shader_data = realloc(st->shader_data, new_cap);
        st->shader_capacity = new_cap;
    }
    if (st->shader_size > 0) {
        st->shader_data[st->shader_size++] = '\n';
    }

    const char *src = sc->data;
    size_t i = 0;
    while (i < sc->size) {
        if (i + 6 <= sc->size && src[i] == '_' && src[i + 1] == '_' && src[i + 2] == 'N' &&
            src[i + 3] == 'S' && src[i + 4] == '_' && src[i + 5] == '_' && ns) {
            memcpy(st->shader_data + st->shader_size, ns, ns_len);
            st->shader_size += ns_len;
            i += 6;
        } else {
            st->shader_data[st->shader_size++] = src[i++];
        }
    }
    st->shader_data[st->shader_size] = '\0';
    ydebug("yrender_pipeline: shader +%zu bytes from '%s' (total %zu)", sc->size, ns,
           st->shader_size);
}

static void collect(struct flatten_state *st, const struct yetty_yrender_gpu_resource_set *rs)
{
    for (size_t i = 0; i < st->visited_count; i++) {
        if (st->visited[i] == rs) {
            return;
        }
    }
    if (st->visited_count < MAX_VISITED_RS) {
        st->visited[st->visited_count++] = rs;
    }

    /* Children first */
    for (size_t i = 0; i < rs->children_count; i++) {
        if (rs->children[i]) {
            collect(st, rs->children[i]);
        }
    }

    for (size_t i = 0; i < rs->buffer_count && st->buffer_count < MAX_FLAT_BUFFERS; i++) {
        struct flat_buffer_view *fb = &st->buffers[st->buffer_count++];
        fb->src = &rs->buffers[i];
        fb->ns = rs->namespace;
        fb->mega_offset = st->storage_total;
        size_t aligned = (rs->buffers[i].size + 3) & ~(size_t)3;
        st->storage_total += aligned;
    }

    for (size_t i = 0; i < rs->texture_count && st->texture_count < MAX_FLAT_TEXTURES; i++) {
        if (rs->textures[i].width == 0 || rs->textures[i].height == 0) {
            continue;
        }
        struct flat_texture_view *ft = &st->textures[st->texture_count++];
        ft->src = &rs->textures[i];
        ft->ns = rs->namespace;
        ft->atlas_index =
            (rs->textures[i].format == WGPUTextureFormat_R8Unorm) ? ATLAS_R8 : ATLAS_RGBA8;
        st->atlas_present[ft->atlas_index] = 1;
    }

    for (size_t i = 0; i < rs->uniform_count && st->uniform_count < MAX_FLAT_UNIFORMS; i++) {
        struct flat_uniform_view *fu = &st->uniforms[st->uniform_count++];
        fu->src = &rs->uniforms[i];
        fu->ns = rs->namespace;
    }

    shader_append(st, &rs->shader, rs->namespace);
}

static uint64_t compute_tree_shader_hash(const struct yetty_yrender_gpu_resource_set *rs)
{
    uint64_t h = rs->shader.hash;
    for (size_t i = 0; i < rs->children_count; i++) {
        if (rs->children[i]) {
            h ^= compute_tree_shader_hash(rs->children[i]);
        }
    }
    return h;
}

/*=============================================================================
 * WGSL bindings preamble
 *===========================================================================*/

static void generate_wgsl_bindings(const struct flatten_state *st, char *out, size_t out_size)
{
    char *p = out;
    size_t rem = out_size;
    uint32_t binding = 0;
    int n;

    if (st->uniform_count > 0) {
        n = snprintf(p, rem, "struct Uniforms {\n");
        if (n > 0 && (size_t)n < rem) {
            p += n;
            rem -= n;
        }
        for (size_t i = 0; i < st->uniform_count; i++) {
            const struct flat_uniform_view *fu = &st->uniforms[i];
            const char *wgsl_type = yetty_yrender_uniform_type_wgsl(fu->src->type);
            n = snprintf(p, rem, "    %s_%s: %s,\n", fu->ns, fu->src->name, wgsl_type);
            if (n > 0 && (size_t)n < rem) {
                p += n;
                rem -= n;
            }
        }
        n = snprintf(p, rem, "};\n@group(0) @binding(%u) var<uniform> uniforms: Uniforms;\n",
                     binding++);
        if (n > 0 && (size_t)n < rem) {
            p += n;
            rem -= n;
        }
    }

    /* Atlas textures (declared even if empty in template — per-instance
     * binders supply concrete textures). */
    for (size_t ai = 0; ai < ATLAS_COUNT; ai++) {
        if (!st->atlas_present[ai]) {
            continue;
        }
        n = snprintf(p, rem, "@group(0) @binding(%u) var %s_texture: texture_2d<f32>;\n", binding++,
                     atlas_names[ai]);
        if (n > 0 && (size_t)n < rem) {
            p += n;
            rem -= n;
        }
        n = snprintf(p, rem, "@group(0) @binding(%u) var %s_sampler: sampler;\n", binding++,
                     atlas_names[ai]);
        if (n > 0 && (size_t)n < rem) {
            p += n;
            rem -= n;
        }
    }

    if (st->buffer_count > 0) {
        n = snprintf(p, rem,
                     "@group(0) @binding(%u) var<storage, read> storage_buffer: array<u32>;\n",
                     binding++);
        if (n > 0 && (size_t)n < rem) {
            p += n;
            rem -= n;
        }
        for (size_t i = 0; i < st->buffer_count; i++) {
            const struct flat_buffer_view *fb = &st->buffers[i];
            n = snprintf(p, rem, "const %s_%s_offset: u32 = %uu;\n", fb->ns, fb->src->name,
                         (uint32_t)(fb->mega_offset / 4));
            if (n > 0 && (size_t)n < rem) {
                p += n;
                rem -= n;
            }
        }
    }
    (void)binding;
}

/*=============================================================================
 * Pipeline impl
 *===========================================================================*/

struct yetty_yrender_pipeline {
    WGPUDevice device;
    WGPUTextureFormat target_format;
    struct yetty_ydraw_gpu_allocator *allocator;

    WGPUShaderModule shader_module;
    WGPUBindGroupLayout bind_group_layout;
    WGPUPipelineLayout pipeline_layout;
    WGPURenderPipeline pipeline;
    WGPUBuffer quad_vertex_buffer;
    uint64_t shader_hash;

    /* Recorded layout shape — binders use this to know how many bindings
     * to create. Exposed via getters below. */
    int has_uniforms;
    int atlas_present[ATLAS_COUNT];
    int has_storage;
};

static struct yetty_ycore_void_result create_bind_group_layout(struct yetty_yrender_pipeline *p,
                                                               const struct flatten_state *st)
{
    WGPUBindGroupLayoutEntry entries[8];
    size_t count = 0;
    uint32_t binding = 0;

    if (st->uniform_count > 0) {
        WGPUBindGroupLayoutEntry *e = &entries[count++];
        memset(e, 0, sizeof(*e));
        e->binding = binding++;
        e->visibility = WGPUShaderStage_Fragment | WGPUShaderStage_Vertex;
        e->buffer.type = WGPUBufferBindingType_Uniform;
        p->has_uniforms = 1;
    }

    for (size_t ai = 0; ai < ATLAS_COUNT; ai++) {
        if (!st->atlas_present[ai]) {
            continue;
        }
        p->atlas_present[ai] = 1;
        WGPUBindGroupLayoutEntry *te = &entries[count++];
        memset(te, 0, sizeof(*te));
        te->binding = binding++;
        te->visibility = WGPUShaderStage_Fragment;
        te->texture.sampleType = WGPUTextureSampleType_Float;
        te->texture.viewDimension = WGPUTextureViewDimension_2D;

        WGPUBindGroupLayoutEntry *se = &entries[count++];
        memset(se, 0, sizeof(*se));
        se->binding = binding++;
        se->visibility = WGPUShaderStage_Fragment;
        se->sampler.type = WGPUSamplerBindingType_Filtering;
    }

    if (st->buffer_count > 0) {
        WGPUBindGroupLayoutEntry *e = &entries[count++];
        memset(e, 0, sizeof(*e));
        e->binding = binding++;
        e->visibility = WGPUShaderStage_Fragment | WGPUShaderStage_Vertex;
        e->buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
        p->has_storage = 1;
    }

    if (count == 0) {
        return YETTY_ERR(yetty_ycore_void, "no bindings in template");
    }

    WGPUBindGroupLayoutDescriptor desc = {0};
    desc.entryCount = count;
    desc.entries = entries;
    p->bind_group_layout = wgpuDeviceCreateBindGroupLayout(p->device, &desc);
    if (!p->bind_group_layout) {
        return YETTY_ERR(yetty_ycore_void, "createBindGroupLayout failed");
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result create_quad_vb(struct yetty_yrender_pipeline *p)
{
    float verts[] = {
        -1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f, 1.0f,
    };
    WGPUBufferDescriptor d = {0};
    d.label.data = "quad vertices (shared)";
    d.label.length = 22;
    d.size = sizeof(verts);
    d.usage = WGPUBufferUsage_Vertex;
    d.mappedAtCreation = 1;
    p->quad_vertex_buffer = p->allocator->ops->create_buffer(p->allocator, &d);
    if (!p->quad_vertex_buffer) {
        return YETTY_ERR(yetty_ycore_void, "create_buffer (quad) failed");
    }
    /* createBuffer never returns NULL on failure (Dawn hands back an error
     * buffer and reports via the async uncaptured-error callback), so the
     * non-NULL check above is not sufficient. getMappedRange DOES return NULL
     * for a buffer that failed to allocate — and on wasm address 0 is writable
     * linear memory, so an unchecked memcpy here silently corrupts instead of
     * trapping. Check it and propagate. */
    void *m = wgpuBufferGetMappedRange(p->quad_vertex_buffer, 0, sizeof(verts));
    if (!m) {
        return YETTY_ERR(
            yetty_ycore_void,
            "wgpuBufferGetMappedRange (quad) returned NULL — buffer allocation failed");
    }
    memcpy(m, verts, sizeof(verts));
    wgpuBufferUnmap(p->quad_vertex_buffer);
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result create_pipeline(struct yetty_yrender_pipeline *p,
                                                      const struct flatten_state *st)
{
    if (st->shader_size == 0) {
        return YETTY_ERR(yetty_ycore_void, "no shader code in template");
    }

    char bindings[MAX_BINDING_CODE];
    generate_wgsl_bindings(st, bindings, sizeof(bindings));
    size_t bindings_len = strlen(bindings);
    size_t total = bindings_len + st->shader_size + 2;
    char *merged = malloc(total);
    if (!merged) {
        return YETTY_ERR(yetty_ycore_void, "malloc merged shader");
    }
    memcpy(merged, bindings, bindings_len);
    merged[bindings_len] = '\n';
    memcpy(merged + bindings_len + 1, st->shader_data, st->shader_size);
    merged[total - 1] = '\0';

    /* Dump merged shader for inspection. Single file gets overwritten
     * on each pipeline create; debugging is per-failure so the last
     * write is the relevant one. */
    {
        FILE *df = fopen("tmp/yrender_pipeline_merged.wgsl", "wb");
        if (df) {
            fwrite(merged, 1, total - 1, df);
            fclose(df);
            ydebug("yrender_pipeline: dumped merged shader (%zu bytes) to "
                   "tmp/yrender_pipeline_merged.wgsl",
                   total - 1);
        }
    }

    WGPUShaderSourceWGSL wgsl = {0};
    wgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgsl.code.data = merged;
    wgsl.code.length = total - 1;

    WGPUShaderModuleDescriptor sd = {0};
    sd.label.data = "yrender_pipeline shader";
    sd.label.length = 23;
    sd.nextInChain = &wgsl.chain;

    /* The WGSL may be untrusted (a user shader arriving over the wire —
     * yshadertoy). Capture validation errors in a scope so a malformed
     * shader surfaces as a Result error instead of hitting the fail-fast
     * uncaptured-error callback and killing the terminal. */
    yetty_ywebgpu_error_scope_push(p->device);
    p->shader_module = wgpuDeviceCreateShaderModule(p->device, &sd);
    free(merged);

    struct yetty_ycore_void_result pop_res =
        yetty_ywebgpu_error_scope_pop(p->allocator->instance, p->device);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, pop_res, "shader module validation");
    if (!p->shader_module) {
        return YETTY_ERR(yetty_ycore_void, "shader compile failed");
    }

    /* Pipeline layout */
    WGPUPipelineLayoutDescriptor pld = {0};
    pld.bindGroupLayoutCount = 1;
    pld.bindGroupLayouts = &p->bind_group_layout;
    p->pipeline_layout = wgpuDeviceCreatePipelineLayout(p->device, &pld);
    if (!p->pipeline_layout) {
        return YETTY_ERR(yetty_ycore_void, "createPipelineLayout failed");
    }

    /* Vertex buffer */
    WGPUVertexAttribute pos = {0};
    pos.format = WGPUVertexFormat_Float32x2;
    pos.shaderLocation = 0;
    WGPUVertexBufferLayout vb = {0};
    vb.arrayStride = 2 * sizeof(float);
    vb.attributeCount = 1;
    vb.attributes = &pos;

    /* Premultiplied-alpha src-over. Shaders ship their fragments
     * pre-multiplied (see ygrid.wgsl / ydraw-layer.wgsl: the final
     * `return vec4(result_color * result_alpha, result_alpha)`),
     * so the blend factor for the source must be ONE — not SrcAlpha,
     * which would multiply the pre-multiplied RGB by the alpha a
     * SECOND time and turn rgb*a²+dst*(1-a) into the new framebuffer
     * value. For fully-opaque fragments (a=1) the math collapses to
     * the same answer on most backends, but Dawn-on-Metal vs
     * Dawn-on-Vulkan diverge at sub-pixel SDF edges (where a≠1) and
     * thin-bar SDF prims (the chrome accent underline) can vanish
     * entirely on macOS while rendering correctly on Linux. */
    WGPUBlendState blend = {0};
    blend.color.srcFactor = WGPUBlendFactor_One;
    blend.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blend.color.operation = WGPUBlendOperation_Add;
    blend.alpha.srcFactor = WGPUBlendFactor_One;
    blend.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blend.alpha.operation = WGPUBlendOperation_Add;

    WGPUColorTargetState ct = {0};
    ct.format = p->target_format;
    ct.blend = &blend;
    ct.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fs = {0};
    fs.module = p->shader_module;
    fs.entryPoint.data = "fs_main";
    fs.entryPoint.length = 7;
    fs.targetCount = 1;
    fs.targets = &ct;

    WGPURenderPipelineDescriptor pd = {0};
    pd.label.data = "yrender_pipeline";
    pd.label.length = 16;
    pd.layout = p->pipeline_layout;
    pd.vertex.module = p->shader_module;
    pd.vertex.entryPoint.data = "vs_main";
    pd.vertex.entryPoint.length = 7;
    pd.vertex.bufferCount = 1;
    pd.vertex.buffers = &vb;
    pd.fragment = &fs;
    pd.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pd.primitive.frontFace = WGPUFrontFace_CCW;
    pd.primitive.cullMode = WGPUCullMode_None;
    pd.multisample.count = 1;
    pd.multisample.mask = ~0u;

    /* Same untrusted-input concern as CreateShaderModule above: a shader
     * that parses but lacks vs_main / fs_main (or mismatches the layout)
     * only fails here. */
    yetty_ywebgpu_error_scope_push(p->device);
    p->pipeline = wgpuDeviceCreateRenderPipeline(p->device, &pd);
    struct yetty_ycore_void_result pipeline_pop_res =
        yetty_ywebgpu_error_scope_pop(p->allocator->instance, p->device);
    if (YETTY_IS_ERR(pipeline_pop_res)) {
        if (p->pipeline) {
            /* Dawn returns an invalid object on validation failure —
             * release it so destroy doesn't double-handle. */
            wgpuRenderPipelineRelease(p->pipeline);
            p->pipeline = NULL;
        }
        return YETTY_ERR(yetty_ycore_void, "render pipeline validation", pipeline_pop_res);
    }
    if (!p->pipeline) {
        return YETTY_ERR(yetty_ycore_void, "createRenderPipeline failed");
    }
    return YETTY_OK_VOID();
}

/*=============================================================================
 * Public API
 *===========================================================================*/

struct yetty_yrender_pipeline_ptr_result yetty_yrender_pipeline_create(
    WGPUDevice device, WGPUTextureFormat target_format, struct yetty_ydraw_gpu_allocator *allocator,
    const struct yetty_yrender_gpu_resource_set *template_rs)
{
    if (!device || !allocator || !template_rs) {
        return YETTY_ERR(yetty_yrender_pipeline_ptr, "NULL arg to pipeline_create");
    }

    struct yetty_yrender_pipeline *p = calloc(1, sizeof(*p));
    if (!p) {
        return YETTY_ERR(yetty_yrender_pipeline_ptr, "calloc");
    }
    p->device = device;
    p->target_format = target_format;
    p->allocator = allocator;
    p->shader_hash = compute_tree_shader_hash(template_rs);

    struct flatten_state st = {0};
    collect(&st, template_rs);

    struct yetty_ycore_void_result r = create_bind_group_layout(p, &st);
    if (YETTY_IS_ERR(r)) {
        free(st.shader_data);
        yetty_yrender_pipeline_destroy(p);
        return YETTY_ERR(yetty_yrender_pipeline_ptr, "bind_group_layout", r);
    }

    r = create_pipeline(p, &st);
    free(st.shader_data);
    if (YETTY_IS_ERR(r)) {
        yetty_yrender_pipeline_destroy(p);
        return YETTY_ERR(yetty_yrender_pipeline_ptr, "pipeline", r);
    }

    r = create_quad_vb(p);
    if (YETTY_IS_ERR(r)) {
        yetty_yrender_pipeline_destroy(p);
        return YETTY_ERR(yetty_yrender_pipeline_ptr, "quad_vb", r);
    }

    yinfo("yrender_pipeline: compiled (hash=0x%llx)", (unsigned long long)p->shader_hash);
    return YETTY_OK(yetty_yrender_pipeline_ptr, p);
}

void yetty_yrender_pipeline_destroy(struct yetty_yrender_pipeline *p)
{
    if (!p) {
        return;
    }
    if (p->pipeline) {
        wgpuRenderPipelineRelease(p->pipeline);
    }
    if (p->pipeline_layout) {
        wgpuPipelineLayoutRelease(p->pipeline_layout);
    }
    if (p->bind_group_layout) {
        wgpuBindGroupLayoutRelease(p->bind_group_layout);
    }
    if (p->shader_module) {
        wgpuShaderModuleRelease(p->shader_module);
    }
    if (p->quad_vertex_buffer && p->allocator) {
        p->allocator->ops->release_buffer(p->allocator, p->quad_vertex_buffer);
    }
    free(p);
}

WGPURenderPipeline yetty_yrender_pipeline_get_pipeline(const struct yetty_yrender_pipeline *p)
{
    return p ? p->pipeline : NULL;
}

WGPUBindGroupLayout yetty_yrender_pipeline_get_bind_group_layout(
    const struct yetty_yrender_pipeline *p)
{
    return p ? p->bind_group_layout : NULL;
}

WGPUBuffer yetty_yrender_pipeline_get_quad_vertex_buffer(const struct yetty_yrender_pipeline *p)
{
    return p ? p->quad_vertex_buffer : NULL;
}

void yetty_yrender_pipeline_bind(const struct yetty_yrender_pipeline *p, WGPURenderPassEncoder pass)
{
    if (!p || !pass) {
        return;
    }
    if (p->pipeline) {
        wgpuRenderPassEncoderSetPipeline(pass, p->pipeline);
    }
    if (p->quad_vertex_buffer) {
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, p->quad_vertex_buffer, 0, WGPU_WHOLE_SIZE);
    }
}
