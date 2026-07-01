#include <yetty/yrender/gpu-resource-binder.h>
#include <yetty/yrender/gpu-allocator.h>
#include <yetty/yrender/types.h>
#include <yetty/webgpu/error.h>
#include <yetty/ytrace/ytrace.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#ifdef __ANDROID__
#include <android/log.h>
#endif

#define MAX_RESOURCE_SETS 32
#define MAX_BINDING_CODE 16384
#define MAX_FLAT_BUFFERS 64
#define MAX_FLAT_TEXTURES 64
#define MAX_FLAT_UNIFORMS 256
#define ATLAS_MIN_SIZE 32
#define ATLAS_MAX_SIZE 8192

/* Flat buffer entry — one per buffer across all resource sets */
struct flat_buffer {
    struct yetty_yrender_buffer *src; /* mutable to clear dirty */
    const char *ns;                   /* namespace for WGSL name */
    size_t mega_offset;               /* byte offset in mega buffer */
    size_t cap;                       /* GPU-allocated capacity for this slot.
                                       * Always >= src->size, rounded up to a
                                       * power of 2 so small growth doesn't
                                       * shift subsequent buffers' mega_offsets
                                       * — that bake into the shader as `const
                                       * X_offset: u32 = …` and a moved offset
                                       * means a fresh shader compile. */
};

/* Round buffer size up to a power-of-2 capacity. Worst-case 2× memory overhead
 * in exchange for stable mega_offsets across small growth — once a buffer
 * slot's GPU capacity is allocated, subsequent envelopes can add a few KB
 * without forcing every later buffer's offset to shift (and every shader to
 * recompile via libnvidia-gpucomp). 256 B floor keeps the math simple for
 * the tiny font-metadata buffers. */
static size_t round_buffer_capacity(size_t s)
{
    size_t c = 256;
    while (c < s) {
        c *= 2;
    }
    return c;
}

/* Flat texture entry — one per texture across all resource sets */
struct flat_texture {
    struct yetty_yrender_texture *src; /* mutable to clear dirty */
    const char *ns;
    uint32_t atlas_x, atlas_y; /* position in atlas */
    size_t atlas_index;        /* which atlas (by format) */
};

/* Flat uniform entry — one per uniform across all resource sets */
struct flat_uniform {
    const struct yetty_yrender_uniform *src;
    const char *ns;
};

/* Fixed atlas slots — one per format, always bound */
struct texture_atlas {
    uint32_t width;
    uint32_t height;
    WGPUTexture texture;
    WGPUTextureView view;
    WGPUSampler sampler;
};

struct yetty_yrender_gpu_resource_binder_impl {
    struct yetty_yrender_gpu_resource_binder base;
    WGPUDevice device;
    WGPUQueue queue;
    WGPUTextureFormat surface_format;
    struct yetty_ydraw_gpu_allocator *allocator;

    /* If non-NULL, binder runs in two-tier mode: skips shader compile +
     * pipeline create; uses pipeline's bind_group_layout to create its own
     * bind_group; reads pipeline / quad VB through the pipeline accessors.
     * Cached locally for fast path access. */
    const struct yetty_yrender_pipeline *external_pipeline;

    /* Submitted resource sets (persistent pointers, submitted once) */
    struct yetty_yrender_gpu_resource_set *resource_sets[MAX_RESOURCE_SETS];
    size_t resource_set_count;
    int submitted; /* resource sets already submitted */

    /* Flattened resources (collected from all resource sets + children) */
    struct flat_buffer flat_buffers[MAX_FLAT_BUFFERS];
    size_t flat_buffer_count;
    size_t storage_buffer_size;

    struct flat_texture flat_textures[MAX_FLAT_TEXTURES];
    size_t flat_texture_count;

    struct flat_uniform flat_uniforms[MAX_FLAT_UNIFORMS];
    size_t flat_uniform_count;

    /* Merged shader code from all providers */
    struct yetty_yrender_buffer shader_code;

    /* Fixed atlas slots — one per format, always bound */
#define ATLAS_R8 0
#define ATLAS_RGBA8 1
#define ATLAS_COUNT 2
    struct texture_atlas atlases[ATLAS_COUNT];

    /* GPU resources */
    WGPUBuffer storage_buffer;
    WGPUBuffer uniform_buffer;
    size_t uniform_buffer_size;

    WGPUBindGroupLayout bind_group_layout;
    WGPUBindGroup bind_group;
    WGPUShaderModule shader_module;
    WGPUPipelineLayout pipeline_layout;
    WGPURenderPipeline pipeline;
    /* When set, shader_module + pipeline are owned by the allocator's shared
     * pipeline cache, not by this binder — destroy/re-finalize must NOT
     * release them. */
    int pipeline_cached;
    WGPUBuffer quad_vertex_buffer;
    int finalized;
    uint64_t last_shader_hash;

    /* Cached capacities for detecting layout-breaking changes. The buffer
     * cap is the GPU-allocated slot size (rounded up); a logical size that
     * still fits inside its cap is NOT a layout change. */
    size_t last_buffer_caps[MAX_FLAT_BUFFERS];
    uint32_t last_tex_width[MAX_FLAT_TEXTURES];
    uint32_t last_tex_height[MAX_FLAT_TEXTURES];

    /* Visited resource sets during collection (to avoid duplicates) */
    const struct yetty_yrender_gpu_resource_set *visited_rs[MAX_RESOURCE_SETS * 4];
    size_t visited_rs_count;
};

/* Forward declarations */
static void binder_destroy(struct yetty_yrender_gpu_resource_binder *self);
static struct yetty_ycore_void_result binder_submit(
    struct yetty_yrender_gpu_resource_binder *self,
    const struct yetty_yrender_gpu_resource_set *rs);
static struct yetty_ycore_void_result binder_finalize(
    struct yetty_yrender_gpu_resource_binder *self);
static struct yetty_ycore_void_result binder_release_gpu(
    struct yetty_yrender_gpu_resource_binder *self);
static struct yetty_ycore_void_result binder_update(struct yetty_yrender_gpu_resource_binder *self);
static struct yetty_ycore_void_result binder_bind(struct yetty_yrender_gpu_resource_binder *self,
                                                  WGPURenderPassEncoder pass, uint32_t group_index);
static WGPURenderPipeline binder_get_pipeline(const struct yetty_yrender_gpu_resource_binder *self);
static WGPUBuffer binder_get_quad_vertex_buffer(
    const struct yetty_yrender_gpu_resource_binder *self);
static struct yetty_ycore_void_result binder_write_buffer_chunk(
    struct yetty_yrender_gpu_resource_binder *self, size_t buffer_index, size_t byte_offset,
    const void *data, size_t size);

static const struct yetty_yrender_gpu_resource_binder_ops binder_ops = {
    .destroy = binder_destroy,
    .submit = binder_submit,
    .finalize = binder_finalize,
    .release_gpu = binder_release_gpu,
    .update = binder_update,
    .bind = binder_bind,
    .get_pipeline = binder_get_pipeline,
    .get_quad_vertex_buffer = binder_get_quad_vertex_buffer,
    .write_buffer_chunk = binder_write_buffer_chunk,
};

/* Append shader code to merged buffer, substituting every literal
 * "__NS__" with `ns`. Lets a per-instance shader (e.g. msdf-font.wgsl)
 * use `__NS___glyph_sample`, `uniforms.__NS___base_size` etc. and have
 * the namespace pasted in here so multiple instances of the same shader
 * type don't collide on identifier names. Mirrors shader_append in
 * pipeline.c — both must do the substitution because the binder builds
 * its own merged WGSL for the final shader module. */
static void append_shader(struct yetty_yrender_gpu_resource_binder_impl *impl,
                          const struct yetty_yrender_shader_code *sc, const char *ns)
{
    if (!sc->data || sc->size == 0) {
        return;
    }
    size_t ns_len = ns ? strlen(ns) : 0;
    /* Cheap upper bound: every byte may expand to ns_len bytes. */
    size_t expanded_max = sc->size + sc->size * (ns_len > 6 ? ns_len : 6) / 6;
    size_t needed = impl->shader_code.size + expanded_max + 2;
    if (needed > impl->shader_code.capacity) {
        size_t new_cap = needed * 2;
        impl->shader_code.data = realloc(impl->shader_code.data, new_cap);
        impl->shader_code.capacity = new_cap;
    }
    if (impl->shader_code.size > 0) {
        impl->shader_code.data[impl->shader_code.size++] = '\n';
    }

    const char *src = sc->data;
    size_t i = 0;
    while (i < sc->size) {
        if (i + 6 <= sc->size && src[i] == '_' && src[i + 1] == '_' && src[i + 2] == 'N' &&
            src[i + 3] == 'S' && src[i + 4] == '_' && src[i + 5] == '_' && ns) {
            memcpy(impl->shader_code.data + impl->shader_code.size, ns, ns_len);
            impl->shader_code.size += ns_len;
            i += 6;
        } else {
            impl->shader_code.data[impl->shader_code.size++] = src[i++];
        }
    }
    impl->shader_code.data[impl->shader_code.size] = '\0';
    ydebug("GpuResourceBinder: shader +%zu bytes from '%s' (total %zu)", sc->size, ns,
           impl->shader_code.size);
}

/* Compute combined hash of entire resource set tree */
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

/* Collect all resources from a resource set and its children recursively.
 * Children first (they define functions), parent last (calls them). */
static void collect_resources(struct yetty_yrender_gpu_resource_binder_impl *impl,
                              struct yetty_yrender_gpu_resource_set *rs)
{
    /* Check if already visited (avoid duplicating shared children) */
    for (size_t i = 0; i < impl->visited_rs_count; i++) {
        if (impl->visited_rs[i] == rs) {
            return;
        }
    }
    if (impl->visited_rs_count < sizeof(impl->visited_rs) / sizeof(impl->visited_rs[0])) {
        impl->visited_rs[impl->visited_rs_count++] = rs;
    }

    /* Children first — depth-first, they define shader functions */
    for (size_t i = 0; i < rs->children_count; i++) {
        if (rs->children[i]) {
            collect_resources(impl, rs->children[i]);
        }
    }

    /* Buffers - always collect even if empty (shader needs offset constants).
     * mega_offset is stable across re-finalizes as long as the cap fits — the
     * shader hard-codes `const X_offset: u32 = …` for every buffer, so an
     * offset shift means a full WGSL → SPIR-V → NVIDIA-IR recompile chain. */
    for (size_t i = 0; i < rs->buffer_count && impl->flat_buffer_count < MAX_FLAT_BUFFERS; i++) {
        struct flat_buffer *fb = &impl->flat_buffers[impl->flat_buffer_count++];
        fb->src = &rs->buffers[i];
        fb->ns = rs->namespace;
        fb->mega_offset = impl->storage_buffer_size;
        fb->cap = round_buffer_capacity(rs->buffers[i].size);
        impl->storage_buffer_size += fb->cap;
    }

    /* Textures */
    for (size_t i = 0; i < rs->texture_count && impl->flat_texture_count < MAX_FLAT_TEXTURES; i++) {
        if (rs->textures[i].width == 0 || rs->textures[i].height == 0) {
            continue;
        }
        struct flat_texture *ft = &impl->flat_textures[impl->flat_texture_count++];
        ft->src = &rs->textures[i];
        ft->ns = rs->namespace;
        ft->atlas_index =
            (rs->textures[i].format == WGPUTextureFormat_R8Unorm) ? ATLAS_R8 : ATLAS_RGBA8;
    }

    /* Uniforms */
    for (size_t i = 0; i < rs->uniform_count && impl->flat_uniform_count < MAX_FLAT_UNIFORMS; i++) {
        struct flat_uniform *fu = &impl->flat_uniforms[impl->flat_uniform_count++];
        fu->src = &rs->uniforms[i];
        fu->ns = rs->namespace;
    }

    /* Shader code — parent appended AFTER children */
    append_shader(impl, &rs->shader, rs->namespace);
}

/* Shelf-pack textures for one atlas (by format index) */
static void pack_one_atlas(struct yetty_yrender_gpu_resource_binder_impl *impl, size_t ai)
{
    /* Sort textures belonging to this atlas by height descending */
    for (size_t i = 0; i < impl->flat_texture_count; i++) {
        if (impl->flat_textures[i].atlas_index != ai) {
            continue;
        }
        for (size_t j = i + 1; j < impl->flat_texture_count; j++) {
            if (impl->flat_textures[j].atlas_index != ai) {
                continue;
            }
            if (impl->flat_textures[j].src->height > impl->flat_textures[i].src->height) {
                struct flat_texture tmp = impl->flat_textures[i];
                impl->flat_textures[i] = impl->flat_textures[j];
                impl->flat_textures[j] = tmp;
            }
        }
    }

    /* Start from at least the widest texture */
    uint32_t min_size = ATLAS_MIN_SIZE;
    for (size_t i = 0; i < impl->flat_texture_count; i++) {
        if (impl->flat_textures[i].atlas_index != ai) {
            continue;
        }
        if (impl->flat_textures[i].src->width > min_size) {
            min_size = impl->flat_textures[i].src->width;
        }
        if (impl->flat_textures[i].src->height > min_size) {
            min_size = impl->flat_textures[i].src->height;
        }
    }
    /* Round up to power of 2 */
    uint32_t size = ATLAS_MIN_SIZE;
    while (size < min_size) {
        size *= 2;
    }

    while (size <= ATLAS_MAX_SIZE) {
        uint32_t x = 0, y = 0, row_h = 0;
        int fits = 1;

        for (size_t i = 0; i < impl->flat_texture_count; i++) {
            if (impl->flat_textures[i].atlas_index != ai) {
                continue;
            }
            uint32_t w = impl->flat_textures[i].src->width;
            uint32_t h = impl->flat_textures[i].src->height;

            if (x + w > size) {
                y += row_h;
                x = 0;
                row_h = 0;
            }
            if (y + h > size) {
                fits = 0;
                break;
            }

            impl->flat_textures[i].atlas_x = x;
            impl->flat_textures[i].atlas_y = y;
            x += w;
            if (h > row_h) {
                row_h = h;
            }
        }

        if (fits) {
            impl->atlases[ai].width = size;
            impl->atlases[ai].height = size;
            return;
        }
        size *= 2;
    }

    yerror("GpuResourceBinder: atlas[%zu] overflow", ai);
    impl->atlases[ai].width = ATLAS_MAX_SIZE;
    impl->atlases[ai].height = ATLAS_MAX_SIZE;
}

static const WGPUTextureFormat atlas_formats[ATLAS_COUNT] = {
    WGPUTextureFormat_R8Unorm,
    WGPUTextureFormat_RGBA8Unorm,
};

static const char *atlas_names[ATLAS_COUNT] = {
    "atlas_r8",
    "atlas_rgba8",
};

/* Check if any textures use a given atlas index */
static int atlas_has_textures(struct yetty_yrender_gpu_resource_binder_impl *impl, size_t ai)
{
    for (size_t i = 0; i < impl->flat_texture_count; i++) {
        if (impl->flat_textures[i].atlas_index == ai) {
            return 1;
        }
    }
    return 0;
}

static struct yetty_ycore_void_result create_atlas(
    struct yetty_yrender_gpu_resource_binder_impl *impl, size_t ai)
{
    pack_one_atlas(impl, ai);
    struct texture_atlas *a = &impl->atlases[ai];

    WGPUTextureDescriptor tex_desc = {0};
    tex_desc.label.data = atlas_names[ai];
    tex_desc.label.length = strlen(atlas_names[ai]);
    tex_desc.size.width = a->width;
    tex_desc.size.height = a->height;
    tex_desc.size.depthOrArrayLayers = 1;
    tex_desc.mipLevelCount = 1;
    tex_desc.sampleCount = 1;
    tex_desc.dimension = WGPUTextureDimension_2D;
    tex_desc.format = atlas_formats[ai];
    tex_desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;

    a->texture = impl->allocator->ops->create_texture(impl->allocator, &tex_desc);
    if (!a->texture) {
        return YETTY_ERR(yetty_ycore_void, "failed to create atlas texture");
    }

    WGPUTextureViewDescriptor view_desc = {0};
    view_desc.format = atlas_formats[ai];
    view_desc.dimension = WGPUTextureViewDimension_2D;
    view_desc.mipLevelCount = 1;
    view_desc.arrayLayerCount = 1;
    a->view = wgpuTextureCreateView(a->texture, &view_desc);

    WGPUSamplerDescriptor sampler_desc = {0};
    sampler_desc.addressModeU = WGPUAddressMode_ClampToEdge;
    sampler_desc.addressModeV = WGPUAddressMode_ClampToEdge;
    sampler_desc.addressModeW = WGPUAddressMode_ClampToEdge;
    sampler_desc.magFilter = WGPUFilterMode_Linear;
    sampler_desc.minFilter = WGPUFilterMode_Linear;
    sampler_desc.mipmapFilter = WGPUMipmapFilterMode_Nearest;
    sampler_desc.maxAnisotropy = 1;
    a->sampler = wgpuDeviceCreateSampler(impl->device, &sampler_desc);

    ydebug("GpuResourceBinder: %s %ux%u", atlas_names[ai], a->width, a->height);
    return YETTY_OK_VOID();
}

/* Create GPU resources */
static struct yetty_ycore_void_result create_gpu_resources(
    struct yetty_yrender_gpu_resource_binder_impl *impl)
{
    /* Storage buffer - create even if size is 0 when buffers are declared (placeholder for bind group) */
    if (impl->flat_buffer_count > 0) {
        char label[] = "storage_buffer";
        WGPUBufferDescriptor desc = {0};
        desc.label.data = label;
        desc.label.length = sizeof(label) - 1;
        desc.size = impl->storage_buffer_size > 0 ? impl->storage_buffer_size : 4; /* min 4 bytes */
        desc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
        impl->storage_buffer = impl->allocator->ops->create_buffer(impl->allocator, &desc);
        if (!impl->storage_buffer) {
            return YETTY_ERR(yetty_ycore_void, "failed to create storage buffer");
        }
        ydebug("GpuResourceBinder: storage_buffer %zu bytes", impl->storage_buffer_size);
    }

    /* Per-format atlases */
    for (size_t ai = 0; ai < ATLAS_COUNT; ai++) {
        if (atlas_has_textures(impl, ai)) {
            struct yetty_ycore_void_result res = create_atlas(impl, ai);
            if (!YETTY_IS_OK(res)) {
                return res;
            }
        }
    }

    /* Uniform buffer (with WGSL alignment) */
    if (impl->flat_uniform_count > 0) {
        impl->uniform_buffer_size = 0;
        for (size_t i = 0; i < impl->flat_uniform_count; i++) {
            size_t align = yetty_yrender_uniform_type_align(impl->flat_uniforms[i].src->type);
            impl->uniform_buffer_size = (impl->uniform_buffer_size + align - 1) & ~(align - 1);
            impl->uniform_buffer_size +=
                yetty_yrender_uniform_type_size(impl->flat_uniforms[i].src->type);
        }
        impl->uniform_buffer_size = (impl->uniform_buffer_size + 15) & ~(size_t)15;

        char label[] = "uniforms";
        WGPUBufferDescriptor desc = {0};
        desc.label.data = label;
        desc.label.length = sizeof(label) - 1;
        desc.size = impl->uniform_buffer_size;
        desc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        impl->uniform_buffer = impl->allocator->ops->create_buffer(impl->allocator, &desc);
        if (!impl->uniform_buffer) {
            return YETTY_ERR(yetty_ycore_void, "failed to create uniform buffer");
        }
    }

    return YETTY_OK_VOID();
}

/* Upload all data to GPU */
static struct yetty_ycore_void_result upload_all(
    struct yetty_yrender_gpu_resource_binder_impl *impl)
{
    /* Upload buffers into mega buffer at their offsets */
    for (size_t i = 0; i < impl->flat_buffer_count; i++) {
        const struct flat_buffer *fb = &impl->flat_buffers[i];
        if (fb->src->data && fb->src->size > 0 && impl->storage_buffer) {
            ydebug(
                "GpuResourceBinder: upload buffer '%s_%s' %zu bytes at offset %zu (u32 offset=%zu)",
                fb->ns, fb->src->name, fb->src->size, fb->mega_offset, fb->mega_offset / 4);
            wgpuQueueWriteBuffer(impl->queue, impl->storage_buffer, fb->mega_offset, fb->src->data,
                                 fb->src->size);

            /* Dump first few u32/float values for debugging */
            size_t dump_count = fb->src->size / 4;
            if (dump_count > 10) {
                dump_count = 10;
            }
            const float *fvals = (const float *)fb->src->data;
            const uint32_t *uvals = (const uint32_t *)fb->src->data;
            for (size_t j = 0; j < dump_count; j++) {
                ydebug("  [%zu] u32=0x%08x float=%.6f", j, uvals[j], fvals[j]);
            }
        } else {
            ydebug("GpuResourceBinder: SKIP buffer '%s_%s' data=%p size=%zu mega=%p", fb->ns,
                   fb->src->name, (void *)fb->src->data, fb->src->size,
                   (void *)impl->storage_buffer);
        }
    }

    /* Upload textures into atlas at their packed positions */
    for (size_t i = 0; i < impl->flat_texture_count; i++) {
        const struct flat_texture *ft = &impl->flat_textures[i];
        struct texture_atlas *a = &impl->atlases[ft->atlas_index];
        if (!ft->src->data || !a->texture) {
            ydebug("GpuResourceBinder: SKIP texture '%s_%s' data=%p atlas=%p", ft->ns,
                   ft->src->name, (void *)ft->src->data, (void *)a->texture);
            continue;
        }

        size_t tex_size = yetty_yrender_texture_get_size(ft->src);
        if (tex_size == 0) {
            ydebug("GpuResourceBinder: SKIP texture '%s_%s' tex_size=0", ft->ns, ft->src->name);
            continue;
        }

        uint32_t bpp = (ft->src->format == WGPUTextureFormat_R8Unorm) ? 1 : 4;

        WGPUTexelCopyTextureInfo dest = {0};
        dest.texture = a->texture;
        dest.mipLevel = 0;
        dest.origin.x = ft->atlas_x;
        dest.origin.y = ft->atlas_y;

        WGPUTexelCopyBufferLayout src_layout = {0};
        src_layout.bytesPerRow = ft->src->width * bpp;
        src_layout.rowsPerImage = ft->src->height;

        WGPUExtent3D extent = {ft->src->width, ft->src->height, 1};
        ydebug("GpuResourceBinder: upload texture '%s_%s' %ux%u bpp=%u at atlas(%u,%u) %zu bytes",
               ft->ns, ft->src->name, ft->src->width, ft->src->height, bpp, ft->atlas_x,
               ft->atlas_y, tex_size);

        wgpuQueueWriteTexture(impl->queue, &dest, ft->src->data, tex_size, &src_layout, &extent);
    }

    /* Resolve atlas slice → vec4 uniform `<ns>_<texname>_region`. The
     * generator declares this uniform per texture (default identity
     * (0,0,1,1)); we overwrite it here so the shader can sample with
     * `mix(region.xy, region.zw, local_uv)` against the shared atlas. */
    for (size_t i = 0; i < impl->flat_texture_count; i++) {
        const struct flat_texture *ft = &impl->flat_textures[i];
        const struct texture_atlas *a = &impl->atlases[ft->atlas_index];
        if (!a->texture || a->width == 0 || a->height == 0) {
            continue;
        }
        char region_name[YETTY_YRENDER_NAME_MAX];
        int rn = snprintf(region_name, sizeof(region_name), "%s_region", ft->src->name);
        if (rn <= 0 || (size_t)rn >= sizeof(region_name)) {
            continue;
        }
        for (size_t j = 0; j < impl->flat_uniform_count; j++) {
            /* The binder's flat_uniform.src is `const` because most code
             * paths only read it; here we legitimately mutate it (atlas
             * position is binder-time information that flows back into
             * the uniform). Cast away const at the single write site. */
            struct yetty_yrender_uniform *u =
                (struct yetty_yrender_uniform *)impl->flat_uniforms[j].src;
            if (u->type != YETTY_YRENDER_UNIFORM_VEC4) {
                continue;
            }
            if (strcmp(impl->flat_uniforms[j].ns, ft->ns) != 0) {
                continue;
            }
            if (strcmp(u->name, region_name) != 0) {
                continue;
            }
            float aw = (float)a->width;
            float ah = (float)a->height;
            u->vec4[0] = (float)ft->atlas_x / aw;
            u->vec4[1] = (float)ft->atlas_y / ah;
            u->vec4[2] = (float)(ft->atlas_x + ft->src->width) / aw;
            u->vec4[3] = (float)(ft->atlas_y + ft->src->height) / ah;
            ydebug("GpuResourceBinder: region '%s_%s' = (%.4f, %.4f, %.4f, %.4f)", ft->ns,
                   region_name, u->vec4[0], u->vec4[1], u->vec4[2], u->vec4[3]);
            break;
        }
    }

    /* Upload uniforms (with WGSL alignment) */
    if (impl->uniform_buffer && impl->flat_uniform_count > 0) {
        uint8_t packed[65536];
        memset(packed, 0, sizeof(packed));
        size_t offset = 0;
        for (size_t i = 0; i < impl->flat_uniform_count; i++) {
            const struct yetty_yrender_uniform *u = impl->flat_uniforms[i].src;
            size_t align = yetty_yrender_uniform_type_align(u->type);
            offset = (offset + align - 1) & ~(align - 1);
            size_t sz = yetty_yrender_uniform_type_size(u->type);
            if (offset + sz > sizeof(packed)) {
                break;
            }
            memcpy(packed + offset, &u->f32, sz); /* union starts at f32 */
            if (u->type == YETTY_YRENDER_UNIFORM_VEC2) {
                ydebug("GpuResourceBinder: uniform[%zu] '%s_%s' vec2(%.1f, %.1f) at offset %zu", i,
                       impl->flat_uniforms[i].ns, u->name, u->vec2[0], u->vec2[1], offset);
            } else if (u->type == YETTY_YRENDER_UNIFORM_F32) {
                ydebug("GpuResourceBinder: uniform[%zu] '%s_%s' f32=%.3f at offset %zu", i,
                       impl->flat_uniforms[i].ns, u->name, u->f32, offset);
            } else if (u->type == YETTY_YRENDER_UNIFORM_U32) {
                ydebug("GpuResourceBinder: uniform[%zu] '%s_%s' u32=0x%08x at offset %zu", i,
                       impl->flat_uniforms[i].ns, u->name, u->u32, offset);
            }
            offset += sz;
        }
        ydebug("GpuResourceBinder: uniform buffer total %zu bytes (gpu buffer %zu)", offset,
               impl->uniform_buffer_size);
        wgpuQueueWriteBuffer(impl->queue, impl->uniform_buffer, 0, packed, offset);
    }

    return YETTY_OK_VOID();
}

/* Generate WGSL binding declarations */
static void generate_wgsl_bindings(struct yetty_yrender_gpu_resource_binder_impl *impl, char *out,
                                   size_t out_size)
{
    char *p = out;
    size_t rem = out_size;
    uint32_t binding = 0;
    int n;

    /* Uniform struct + binding */
    if (impl->flat_uniform_count > 0) {
        n = snprintf(p, rem, "struct Uniforms {\n");
        if (n > 0 && (size_t)n < rem) {
            p += n;
            rem -= n;
        }

        for (size_t i = 0; i < impl->flat_uniform_count; i++) {
            const struct flat_uniform *fu = &impl->flat_uniforms[i];
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

    /* Per-format atlas textures + samplers */
    for (size_t ai = 0; ai < ATLAS_COUNT; ai++) {
        if (!impl->atlases[ai].texture) {
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

    /* Storage buffer - declare if any buffers exist (even if size is 0 during pipeline compilation) */
    if (impl->flat_buffer_count > 0) {
        n = snprintf(p, rem,
                     "@group(0) @binding(%u) var<storage, read> storage_buffer: array<u32>;\n",
                     binding++);
        if (n > 0 && (size_t)n < rem) {
            p += n;
            rem -= n;
        }

        /* Buffer offset constants (in u32 units) */
        for (size_t i = 0; i < impl->flat_buffer_count; i++) {
            const struct flat_buffer *fb = &impl->flat_buffers[i];
            n = snprintf(p, rem, "const %s_%s_offset: u32 = %uu;\n", fb->ns, fb->src->name,
                         (uint32_t)(fb->mega_offset / 4));
            if (n > 0 && (size_t)n < rem) {
                p += n;
                rem -= n;
            }
        }
    }

    /* Texture atlas region constants (in UV space) */
    for (size_t i = 0; i < impl->flat_texture_count; i++) {
        const struct flat_texture *ft = &impl->flat_textures[i];
        const struct texture_atlas *a = &impl->atlases[ft->atlas_index];
        if (!a->texture) {
            continue;
        }
        float aw = (float)a->width;
        float ah = (float)a->height;
        float u0 = (float)ft->atlas_x / aw;
        float v0 = (float)ft->atlas_y / ah;
        float u1 = (float)(ft->atlas_x + ft->src->width) / aw;
        float v1 = (float)(ft->atlas_y + ft->src->height) / ah;
        n = snprintf(p, rem, "const %s_%s_region: vec4<f32> = vec4<f32>(%f, %f, %f, %f);\n", ft->ns,
                     ft->src->name, u0, v0, u1, v1);
        if (n > 0 && (size_t)n < rem) {
            p += n;
            rem -= n;
        }
    }
}

/* Create bind group */
static struct yetty_ycore_void_result create_bind_group(
    struct yetty_yrender_gpu_resource_binder_impl *impl)
{
    /* max: 1 uniform + ATLAS_COUNT*(texture+sampler) + 1 storage = 1+4+1 = 6 */
    WGPUBindGroupLayoutEntry layout_entries[8];
    WGPUBindGroupEntry group_entries[8];
    size_t count = 0;
    uint32_t binding = 0;

    if (impl->uniform_buffer) {
        WGPUBindGroupLayoutEntry *le = &layout_entries[count];
        memset(le, 0, sizeof(*le));
        le->binding = binding;
        le->visibility = WGPUShaderStage_Fragment | WGPUShaderStage_Vertex;
        le->buffer.type = WGPUBufferBindingType_Uniform;

        WGPUBindGroupEntry *ge = &group_entries[count];
        memset(ge, 0, sizeof(*ge));
        ge->binding = binding;
        ge->buffer = impl->uniform_buffer;
        ge->size = impl->uniform_buffer_size;
        count++;
        binding++;
    }

    for (size_t ai = 0; ai < ATLAS_COUNT; ai++) {
        struct texture_atlas *a = &impl->atlases[ai];
        if (!a->texture) {
            continue;
        }

        WGPUBindGroupLayoutEntry *le = &layout_entries[count];
        memset(le, 0, sizeof(*le));
        le->binding = binding;
        le->visibility = WGPUShaderStage_Fragment;
        le->texture.sampleType = WGPUTextureSampleType_Float;
        le->texture.viewDimension = WGPUTextureViewDimension_2D;

        WGPUBindGroupEntry *ge = &group_entries[count];
        memset(ge, 0, sizeof(*ge));
        ge->binding = binding;
        ge->textureView = a->view;
        count++;
        binding++;

        le = &layout_entries[count];
        memset(le, 0, sizeof(*le));
        le->binding = binding;
        le->visibility = WGPUShaderStage_Fragment;
        le->sampler.type = WGPUSamplerBindingType_Filtering;

        ge = &group_entries[count];
        memset(ge, 0, sizeof(*ge));
        ge->binding = binding;
        ge->sampler = a->sampler;
        count++;
        binding++;
    }

    if (impl->storage_buffer) {
        WGPUBindGroupLayoutEntry *le = &layout_entries[count];
        memset(le, 0, sizeof(*le));
        le->binding = binding;
        le->visibility = WGPUShaderStage_Fragment | WGPUShaderStage_Vertex;
        le->buffer.type = WGPUBufferBindingType_ReadOnlyStorage;

        WGPUBindGroupEntry *ge = &group_entries[count];
        memset(ge, 0, sizeof(*ge));
        ge->binding = binding;
        ge->buffer = impl->storage_buffer;
        ge->size = impl->storage_buffer_size > 0 ? impl->storage_buffer_size : 4;
        count++;
        binding++;
    }

    if (count == 0) {
        return YETTY_ERR(yetty_ycore_void, "no bindings");
    }

    /* Layout: own if legacy mode, else borrow from the external pipeline. */
    WGPUBindGroupLayout layout;
    if (impl->external_pipeline) {
        layout = yetty_yrender_pipeline_get_bind_group_layout(impl->external_pipeline);
        if (!layout) {
            return YETTY_ERR(yetty_ycore_void, "external pipeline has no bind_group_layout");
        }
        /* impl->bind_group_layout stays NULL — pipeline owns it. */
    } else {
        WGPUBindGroupLayoutDescriptor layout_desc = {0};
        layout_desc.entryCount = count;
        layout_desc.entries = layout_entries;
        impl->bind_group_layout = wgpuDeviceCreateBindGroupLayout(impl->device, &layout_desc);
        if (!impl->bind_group_layout) {
            return YETTY_ERR(yetty_ycore_void, "failed to create bind group layout");
        }
        layout = impl->bind_group_layout;
    }

    WGPUBindGroupDescriptor group_desc = {0};
    group_desc.layout = layout;
    group_desc.entryCount = count;
    group_desc.entries = group_entries;
    impl->bind_group = wgpuDeviceCreateBindGroup(impl->device, &group_desc);
    if (!impl->bind_group) {
        return YETTY_ERR(yetty_ycore_void, "failed to create bind group");
    }
    return YETTY_OK_VOID();
}

/* 64-bit FNV-1a over the generated shader text — the pipeline cache key. */
static uint64_t fnv1a64(const char *data, size_t len)
{
    uint64_t hash = 1469598103934665603ULL;
    for (size_t i = 0; i < len; i++) {
        hash ^= (uint64_t)(unsigned char)data[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

/* The full-screen quad VB is content-independent and created once per binder.
 * Factored out so the pipeline-cache hit path (which returns before the
 * inline creation below) still has a vertex buffer to draw with. */
static struct yetty_ycore_void_result ensure_quad_vertex_buffer(
    struct yetty_yrender_gpu_resource_binder_impl *impl)
{
    if (impl->quad_vertex_buffer) {
        return YETTY_OK_VOID();
    }
    float quad_vertices[] = {
        -1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f, 1.0f,
    };
    WGPUBufferDescriptor quad_desc = {0};
    quad_desc.label.data = "quad vertices";
    quad_desc.label.length = 13;
    quad_desc.size = sizeof(quad_vertices);
    quad_desc.usage = WGPUBufferUsage_Vertex;
    quad_desc.mappedAtCreation = 1;
    impl->quad_vertex_buffer = impl->allocator->ops->create_buffer(impl->allocator, &quad_desc);
    if (!impl->quad_vertex_buffer) {
        return YETTY_ERR(yetty_ycore_void, "failed to create quad buffer");
    }
    /* createBuffer never returns NULL on failure (Dawn hands back an error
     * buffer and reports via the async uncaptured-error callback), so the
     * non-NULL check above is not sufficient. getMappedRange DOES return NULL
     * for a buffer that failed to allocate — and on wasm address 0 is writable
     * linear memory, so an unchecked memcpy here silently corrupts instead of
     * trapping. Check it and propagate. */
    void *mapped = wgpuBufferGetMappedRange(impl->quad_vertex_buffer, 0, sizeof(quad_vertices));
    if (!mapped) {
        return YETTY_ERR(
            yetty_ycore_void,
            "wgpuBufferGetMappedRange (quad) returned NULL — buffer allocation failed");
    }
    memcpy(mapped, quad_vertices, sizeof(quad_vertices));
    wgpuBufferUnmap(impl->quad_vertex_buffer);
    return YETTY_OK_VOID();
}

/* Compile shader and create pipeline */
static struct yetty_ycore_void_result compile_and_create_pipeline(
    struct yetty_yrender_gpu_resource_binder_impl *impl)
{
    if (impl->shader_code.size == 0) {
        return YETTY_ERR(yetty_ycore_void, "no shader code");
    }

    char bindings[MAX_BINDING_CODE];
    generate_wgsl_bindings(impl, bindings, sizeof(bindings));

    size_t bindings_len = strlen(bindings);
    size_t total_len = bindings_len + impl->shader_code.size + 2;
    char *merged = malloc(total_len);
    if (!merged) {
        return YETTY_ERR(yetty_ycore_void, "failed to allocate merged shader");
    }

    memcpy(merged, bindings, bindings_len);
    merged[bindings_len] = '\n';
    memcpy(merged + bindings_len + 1, impl->shader_code.data, impl->shader_code.size);
    merged[total_len - 1] = '\0';

    ydebug("GpuResourceBinder: merged shader %zu bytes", total_len);

    /* Reuse a previously compiled pipeline when the generated shader is
     * byte-identical (the common case: every ygrid shares one font dispatcher,
     * so a re-minted figure regenerates the same WGSL). This is what makes
     * yfsvm's static interpreter pay off across scene rebuilds — only the
     * bytecode buffer changes, never the shader, so the WGSL→SPIR-V compile
     * happens once per unique composition instead of on every tab switch. */
    uint64_t pipeline_key = fnv1a64(merged, total_len - 1);
    {
        struct yetty_ycore_void_result quad_res = ensure_quad_vertex_buffer(impl);
        if (!YETTY_IS_OK(quad_res)) {
            free(merged);
            return quad_res;
        }
    }
    {
        WGPUShaderModule cached_module = NULL;
        WGPURenderPipeline cached_pipeline = yetty_yrender_gpu_allocator_pipeline_cache_lookup(
            impl->allocator, pipeline_key, &cached_module);
        if (cached_pipeline && cached_module) {
            impl->shader_module = cached_module;
            impl->pipeline = cached_pipeline;
            impl->pipeline_cached = 1;
            free(merged);
            ydebug("GpuResourceBinder: pipeline cache HIT (reused, no recompile)");
#ifdef __ANDROID__
            __android_log_print(4, "yhello", "PROBE cache HIT ns=%s key=%llx len=%zu",
                                impl->resource_set_count ? impl->resource_sets[0]->namespace : "?",
                                (unsigned long long)pipeline_key, total_len - 1);
#endif
            return YETTY_OK_VOID();
        }
    }
#ifdef __ANDROID__
    __android_log_print(4, "yhello", "PROBE cache MISS->COMPILE ns=%s key=%llx len=%zu",
                        impl->resource_set_count ? impl->resource_sets[0]->namespace : "?",
                        (unsigned long long)pipeline_key, total_len - 1);
#endif

    WGPUShaderSourceWGSL wgsl_src = {0};
    wgsl_src.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgsl_src.code.data = merged;
    wgsl_src.code.length = total_len - 1;

    WGPUShaderModuleDescriptor shader_desc = {0};
    shader_desc.label.data = "terminal shader";
    shader_desc.label.length = 15;
    shader_desc.nextInChain = &wgsl_src.chain;

    /* The WGSL may be untrusted (a user shader arriving over the wire —
     * the yshadertoy figure path compiles here). Capture validation
     * errors in a scope so a malformed shader surfaces as a Result error
     * instead of hitting the fail-fast uncaptured-error callback and
     * killing the terminal. */
    yetty_ywebgpu_error_scope_push(impl->device);
    impl->shader_module = wgpuDeviceCreateShaderModule(impl->device, &shader_desc);
    free(merged);

    char capture_message[512];
    if (yetty_ywebgpu_error_scope_pop(impl->device, capture_message, sizeof(capture_message))) {
        return YETTY_ERR(yetty_ycore_void, capture_message);
    }
    if (!impl->shader_module) {
        return YETTY_ERR(yetty_ycore_void, "failed to compile shader");
    }

    /* Quad VB already ensured before the cache lookup above. */

    /* Pipeline */
    WGPUPipelineLayoutDescriptor pl_desc = {0};
    pl_desc.bindGroupLayoutCount = 1;
    pl_desc.bindGroupLayouts = &impl->bind_group_layout;
    impl->pipeline_layout = wgpuDeviceCreatePipelineLayout(impl->device, &pl_desc);
    if (!impl->pipeline_layout) {
        return YETTY_ERR(yetty_ycore_void, "failed to create pipeline layout");
    }

    WGPUVertexAttribute pos_attr = {0};
    pos_attr.format = WGPUVertexFormat_Float32x2;
    pos_attr.offset = 0;
    pos_attr.shaderLocation = 0;

    WGPUVertexBufferLayout vb_layout = {0};
    vb_layout.arrayStride = 2 * sizeof(float);
    vb_layout.stepMode = WGPUVertexStepMode_Vertex;
    vb_layout.attributeCount = 1;
    vb_layout.attributes = &pos_attr;

    /* Premultiplied-alpha src-over — shaders that drive this pipeline
     * (ygrid.wgsl, ydraw-layer.wgsl) emit premultiplied fragments
     * `vec4(rgb * a, a)`. Source factor must be ONE so the blend
     * doesn't multiply by alpha a second time; otherwise sub-pixel SDF
     * edges and thin bars (the chrome accent underline) collapse to
     * invisible on Dawn-on-Metal even though Dawn-on-Vulkan tolerates
     * the mismatch for fully-opaque fragments. Same fix is mirrored in
     * yrender/pipeline.c. */
    WGPUBlendState blend = {0};
    blend.color.srcFactor = WGPUBlendFactor_One;
    blend.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blend.color.operation = WGPUBlendOperation_Add;
    blend.alpha.srcFactor = WGPUBlendFactor_One;
    blend.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blend.alpha.operation = WGPUBlendOperation_Add;

    WGPUColorTargetState color_target = {0};
    color_target.format = impl->surface_format;
    color_target.blend = &blend;
    color_target.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState frag = {0};
    frag.module = impl->shader_module;
    frag.entryPoint.data = "fs_main";
    frag.entryPoint.length = 7;
    frag.targetCount = 1;
    frag.targets = &color_target;

    WGPURenderPipelineDescriptor pipe_desc = {0};
    pipe_desc.label.data = "terminal pipeline";
    pipe_desc.label.length = 17;
    pipe_desc.layout = impl->pipeline_layout;
    pipe_desc.vertex.module = impl->shader_module;
    pipe_desc.vertex.entryPoint.data = "vs_main";
    pipe_desc.vertex.entryPoint.length = 7;
    pipe_desc.vertex.bufferCount = 1;
    pipe_desc.vertex.buffers = &vb_layout;
    pipe_desc.fragment = &frag;
    pipe_desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipe_desc.primitive.frontFace = WGPUFrontFace_CCW;
    pipe_desc.primitive.cullMode = WGPUCullMode_None;
    pipe_desc.multisample.count = 1;
    pipe_desc.multisample.mask = ~0u;

    /* Same untrusted-input concern as CreateShaderModule above: a shader
     * that parses but lacks vs_main / fs_main only fails here. */
    yetty_ywebgpu_error_scope_push(impl->device);
    impl->pipeline = wgpuDeviceCreateRenderPipeline(impl->device, &pipe_desc);
    if (yetty_ywebgpu_error_scope_pop(impl->device, capture_message, sizeof(capture_message))) {
        if (impl->pipeline) {
            /* Dawn returns an invalid object on validation failure. */
            wgpuRenderPipelineRelease(impl->pipeline);
            impl->pipeline = NULL;
        }
        return YETTY_ERR(yetty_ycore_void, capture_message);
    }
    if (!impl->pipeline) {
        return YETTY_ERR(yetty_ycore_void, "failed to create pipeline");
    }

    ydebug("GpuResourceBinder: pipeline created");

    /* Hand the freshly compiled shader + pipeline to the shared cache. From
     * now on this binder treats them as cache-owned (pipeline_cached) so it
     * never releases them — the next binder with the same WGSL reuses them. */
    yetty_yrender_gpu_allocator_pipeline_cache_store(impl->allocator, pipeline_key,
                                                     impl->shader_module, impl->pipeline);
    impl->pipeline_cached = 1;
    return YETTY_OK_VOID();
}

/* === Public API === */

static void binder_destroy(struct yetty_yrender_gpu_resource_binder *self)
{
    struct yetty_yrender_gpu_resource_binder_impl *impl =
        (struct yetty_yrender_gpu_resource_binder_impl *)self;

    /* pipeline + shader_module may be cache-owned (shared across binders);
     * only release them when this binder compiled them itself. */
    if (impl->pipeline && !impl->pipeline_cached) {
        wgpuRenderPipelineRelease(impl->pipeline);
    }
    if (impl->pipeline_layout) {
        wgpuPipelineLayoutRelease(impl->pipeline_layout);
    }
    if (impl->shader_module && !impl->pipeline_cached) {
        wgpuShaderModuleRelease(impl->shader_module);
    }
    if (impl->quad_vertex_buffer && impl->allocator) {
        impl->allocator->ops->release_buffer(impl->allocator, impl->quad_vertex_buffer);
    }
    if (impl->bind_group) {
        wgpuBindGroupRelease(impl->bind_group);
    }
    if (impl->bind_group_layout) {
        wgpuBindGroupLayoutRelease(impl->bind_group_layout);
    }
    if (impl->storage_buffer && impl->allocator) {
        impl->allocator->ops->release_buffer(impl->allocator, impl->storage_buffer);
    }
    if (impl->uniform_buffer && impl->allocator) {
        impl->allocator->ops->release_buffer(impl->allocator, impl->uniform_buffer);
    }
    for (size_t ai = 0; ai < ATLAS_COUNT; ai++) {
        struct texture_atlas *a = &impl->atlases[ai];
        if (a->sampler) {
            wgpuSamplerRelease(a->sampler);
        }
        if (a->view) {
            wgpuTextureViewRelease(a->view);
        }
        if (a->texture && impl->allocator) {
            impl->allocator->ops->release_texture(impl->allocator, a->texture);
        }
    }
    free(impl->shader_code.data);
    free(impl);
}

static struct yetty_ycore_void_result binder_submit(struct yetty_yrender_gpu_resource_binder *self,
                                                    const struct yetty_yrender_gpu_resource_set *rs)
{
    struct yetty_yrender_gpu_resource_binder_impl *impl =
        (struct yetty_yrender_gpu_resource_binder_impl *)self;

    /* Don't re-add if already submitted (idempotent) */
    if (impl->submitted) {
        for (size_t i = 0; i < impl->resource_set_count; i++) {
            if (impl->resource_sets[i] == rs) {
                return YETTY_OK_VOID();
            }
        }
    }

    if (impl->resource_set_count >= MAX_RESOURCE_SETS) {
        return YETTY_ERR(yetty_ycore_void, "max resource sets reached");
    }

    impl->resource_sets[impl->resource_set_count++] = (struct yetty_yrender_gpu_resource_set *)rs;
    return YETTY_OK_VOID();
}

/* Release every GPU object this binder owns and reset the flattened
 * collection state, leaving the submitted resource sets and the quad vertex
 * buffer intact so a later finalize() rebuilds from the same CPU-side data.
 * Cache-owned pipeline / shader module are dropped by reference only. Shared
 * by binder_finalize's re-finalize prologue and binder_release_gpu, and
 * idempotent — safe to call on a fresh or already-released binder. */
static void binder_teardown_gpu_state(struct yetty_yrender_gpu_resource_binder_impl *impl)
{
    if (impl->pipeline) {
        if (!impl->pipeline_cached) {
            wgpuRenderPipelineRelease(impl->pipeline);
        }
        impl->pipeline = NULL;
    }
    if (impl->pipeline_layout) {
        wgpuPipelineLayoutRelease(impl->pipeline_layout);
        impl->pipeline_layout = NULL;
    }
    if (impl->shader_module) {
        if (!impl->pipeline_cached) {
            wgpuShaderModuleRelease(impl->shader_module);
        }
        impl->shader_module = NULL;
    }
    impl->pipeline_cached = 0;
    if (impl->bind_group) {
        wgpuBindGroupRelease(impl->bind_group);
        impl->bind_group = NULL;
    }
    if (impl->bind_group_layout) {
        wgpuBindGroupLayoutRelease(impl->bind_group_layout);
        impl->bind_group_layout = NULL;
    }
    if (impl->storage_buffer && impl->allocator) {
        impl->allocator->ops->release_buffer(impl->allocator, impl->storage_buffer);
        impl->storage_buffer = NULL;
    }
    if (impl->uniform_buffer && impl->allocator) {
        impl->allocator->ops->release_buffer(impl->allocator, impl->uniform_buffer);
        impl->uniform_buffer = NULL;
    }
    for (size_t ai = 0; ai < ATLAS_COUNT; ai++) {
        struct texture_atlas *a = &impl->atlases[ai];
        if (a->sampler) {
            wgpuSamplerRelease(a->sampler);
            a->sampler = NULL;
        }
        if (a->view) {
            wgpuTextureViewRelease(a->view);
            a->view = NULL;
        }
        if (a->texture && impl->allocator) {
            impl->allocator->ops->release_texture(impl->allocator, a->texture);
            a->texture = NULL;
        }
        a->width = 0;
        a->height = 0;
    }
    /* Keep quad_vertex_buffer — it never changes */

    /* Reset flattened resources and shader code */
    impl->flat_buffer_count = 0;
    impl->flat_texture_count = 0;
    impl->flat_uniform_count = 0;
    impl->storage_buffer_size = 0;
    impl->shader_code.size = 0;
    impl->visited_rs_count = 0;
    impl->finalized = 0;
}

/* Hand the per-instance GPU allocations back to the allocator without
 * destroying the binder. finalize() rebuilds them on demand. */
static struct yetty_ycore_void_result binder_release_gpu(
    struct yetty_yrender_gpu_resource_binder *self)
{
    struct yetty_yrender_gpu_resource_binder_impl *impl =
        (struct yetty_yrender_gpu_resource_binder_impl *)self;
    if (!impl->finalized && !impl->storage_buffer && !impl->uniform_buffer) {
        return YETTY_OK_VOID(); /* already released / never finalized */
    }
    binder_teardown_gpu_state(impl);
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result binder_finalize(
    struct yetty_yrender_gpu_resource_binder *self)
{
    struct yetty_yrender_gpu_resource_binder_impl *impl =
        (struct yetty_yrender_gpu_resource_binder_impl *)self;

    if (impl->finalized) {
        ydebug("GpuResourceBinder: finalize CACHE HIT (already finalized)");
        return YETTY_OK_VOID();
    }
    ydebug("GpuResourceBinder: finalize CACHE MISS - rebuilding pipeline");

    /* Release any GPU resources left from a previous finalize (idempotent on a
     * fresh binder) before rebuilding from the retained resource sets. */
    binder_teardown_gpu_state(impl);

    for (size_t i = 0; i < impl->resource_set_count; i++) {
        collect_resources(impl, impl->resource_sets[i]);
    }

    /* Create GPU resources */
    struct yetty_ycore_void_result res = create_gpu_resources(impl);
    if (!YETTY_IS_OK(res)) {
        return res;
    }

    /* Upload data */
    res = upload_all(impl);
    if (!YETTY_IS_OK(res)) {
        return res;
    }

    /* Create bind group */
    res = create_bind_group(impl);
    if (!YETTY_IS_OK(res)) {
        return res;
    }

    /* Compile and create pipeline (only when binder owns it).
     * In two-tier mode the pipeline is supplied externally and stays
     * fixed across instances. */
    if (!impl->external_pipeline) {
        res = compile_and_create_pipeline(impl);
        if (!YETTY_IS_OK(res)) {
            return res;
        }
    }

    impl->finalized = 1;
    impl->submitted = 1;

    /* Store shader hash and resource sizes for change detection */
    impl->last_shader_hash = 0;
    for (size_t i = 0; i < impl->resource_set_count; i++) {
        impl->last_shader_hash ^= compute_tree_shader_hash(impl->resource_sets[i]);
    }
    for (size_t i = 0; i < impl->flat_buffer_count; i++) {
        impl->last_buffer_caps[i] = impl->flat_buffers[i].cap;
    }
    for (size_t i = 0; i < impl->flat_texture_count; i++) {
        impl->last_tex_width[i] = impl->flat_textures[i].src->width;
        impl->last_tex_height[i] = impl->flat_textures[i].src->height;
    }

    return YETTY_OK_VOID();
}

/* Per-frame update: re-upload dirty data to GPU.
 * If any buffer size or texture dimension changed, the entire layout is invalid
 * and we must re-finalize (recreate GPU resources from scratch).
 * Same if shader hash changed. */
static struct yetty_ycore_void_result binder_update(struct yetty_yrender_gpu_resource_binder *self)
{
    struct yetty_yrender_gpu_resource_binder_impl *impl =
        (struct yetty_yrender_gpu_resource_binder_impl *)self;

    if (!impl->finalized) {
        return YETTY_ERR(yetty_ycore_void, "not finalized");
    }

    /* Check if anything structural changed — requires full re-finalize */
    int need_refinalize = 0;

    /* Shader hash */
    uint64_t current_hash = 0;
    for (size_t i = 0; i < impl->resource_set_count; i++) {
        current_hash ^= compute_tree_shader_hash(impl->resource_sets[i]);
    }
    if (current_hash != impl->last_shader_hash) {
        ydebug("GpuResourceBinder: shader hash changed 0x%llx -> 0x%llx",
               (unsigned long long)impl->last_shader_hash, (unsigned long long)current_hash);
        need_refinalize = 1;
    }

    /* Buffer capacities — only force a layout rebuild if a logical buffer
     * has outgrown its rounded slot. A buffer that's still inside its cap
     * keeps the same mega_offset, the shader's baked-in `*_offset` consts
     * stay valid, and we can just re-upload the dirty bytes into the
     * existing GPU buffer at the existing offset. */
    if (!need_refinalize) {
        for (size_t i = 0; i < impl->flat_buffer_count; i++) {
            if (impl->flat_buffers[i].src->size > impl->last_buffer_caps[i]) {
                ydebug("GpuResourceBinder: buffer '%s_%s' size %zu exceeds cap %zu",
                       impl->flat_buffers[i].ns, impl->flat_buffers[i].src->name,
                       impl->flat_buffers[i].src->size, impl->last_buffer_caps[i]);
                need_refinalize = 1;
                break;
            }
        }
    }

    /* Texture dimensions — if ANY changed, atlas layout is wrong */
    if (!need_refinalize) {
        for (size_t i = 0; i < impl->flat_texture_count; i++) {
            if (impl->flat_textures[i].src->width != impl->last_tex_width[i] ||
                impl->flat_textures[i].src->height != impl->last_tex_height[i]) {
                ydebug("GpuResourceBinder: texture '%s_%s' dims changed %ux%u -> %ux%u",
                       impl->flat_textures[i].ns, impl->flat_textures[i].src->name,
                       impl->last_tex_width[i], impl->last_tex_height[i],
                       impl->flat_textures[i].src->width, impl->flat_textures[i].src->height);
                need_refinalize = 1;
                break;
            }
        }
    }

    if (need_refinalize) {
        ydebug("GpuResourceBinder: update CACHE MISS - structural change, re-finalizing");
        impl->finalized = 0;
        return binder_finalize(self);
    }

    ydebug("GpuResourceBinder: update CACHE HIT - no structural change");
    /* No structural change — upload only dirty resources */
    int any_dirty = 0;

    /* Dirty buffers */
    for (size_t i = 0; i < impl->flat_buffer_count; i++) {
        struct flat_buffer *fb = &impl->flat_buffers[i];
        if (!fb->src->dirty) {
            continue;
        }
        any_dirty = 1;
        if (fb->src->data && fb->src->size > 0 && impl->storage_buffer) {
            ydebug("GpuResourceBinder: update buffer '%s_%s' %zu bytes at offset %zu", fb->ns,
                   fb->src->name, fb->src->size, fb->mega_offset);
            wgpuQueueWriteBuffer(impl->queue, impl->storage_buffer, fb->mega_offset, fb->src->data,
                                 fb->src->size);
        }
        fb->src->dirty = 0;
    }

    /* Dirty textures */
    for (size_t i = 0; i < impl->flat_texture_count; i++) {
        struct flat_texture *ft = &impl->flat_textures[i];
        if (!ft->src->dirty) {
            continue;
        }
        any_dirty = 1;

        struct texture_atlas *a = &impl->atlases[ft->atlas_index];
        if (!ft->src->data || !a->texture) {
            ft->src->dirty = 0;
            continue;
        }
        size_t tex_size = yetty_yrender_texture_get_size(ft->src);
        if (tex_size == 0) {
            ft->src->dirty = 0;
            continue;
        }

        uint32_t bpp = (ft->src->format == WGPUTextureFormat_R8Unorm) ? 1 : 4;
        WGPUTexelCopyTextureInfo dest = {0};
        dest.texture = a->texture;
        dest.mipLevel = 0;
        dest.origin.x = ft->atlas_x;
        dest.origin.y = ft->atlas_y;

        WGPUTexelCopyBufferLayout src_layout = {0};
        src_layout.bytesPerRow = ft->src->width * bpp;
        src_layout.rowsPerImage = ft->src->height;

        WGPUExtent3D extent = {ft->src->width, ft->src->height, 1};
        ydebug("GpuResourceBinder: update texture '%s_%s' %ux%u at atlas(%u,%u)", ft->ns,
               ft->src->name, ft->src->width, ft->src->height, ft->atlas_x, ft->atlas_y);
        wgpuQueueWriteTexture(impl->queue, &dest, ft->src->data, tex_size, &src_layout, &extent);
        ft->src->dirty = 0;
    }

    /* Uniforms — always upload (small, may change without dirty flag) */
    if (impl->uniform_buffer && impl->flat_uniform_count > 0) {
        uint8_t packed[65536];
        memset(packed, 0, sizeof(packed));
        size_t offset = 0;
        for (size_t i = 0; i < impl->flat_uniform_count; i++) {
            const struct yetty_yrender_uniform *u = impl->flat_uniforms[i].src;
            size_t align = yetty_yrender_uniform_type_align(u->type);
            offset = (offset + align - 1) & ~(align - 1);
            size_t sz = yetty_yrender_uniform_type_size(u->type);
            if (offset + sz > sizeof(packed)) {
                break;
            }
            memcpy(packed + offset, &u->f32, sz);
            offset += sz;
        }
        wgpuQueueWriteBuffer(impl->queue, impl->uniform_buffer, 0, packed, offset);
    }

    if (any_dirty) {
        ydebug("GpuResourceBinder: update done (dirty resources uploaded)");
    }

    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result binder_bind(struct yetty_yrender_gpu_resource_binder *self,
                                                  WGPURenderPassEncoder pass, uint32_t group_index)
{
    struct yetty_yrender_gpu_resource_binder_impl *impl =
        (struct yetty_yrender_gpu_resource_binder_impl *)self;

    if (!impl->finalized) {
        return YETTY_ERR(yetty_ycore_void, "not finalized");
    }
    if (!impl->bind_group) {
        return YETTY_ERR(yetty_ycore_void, "bind group is null");
    }

    wgpuRenderPassEncoderSetBindGroup(pass, group_index, impl->bind_group, 0, NULL);
    return YETTY_OK_VOID();
}

static WGPURenderPipeline binder_get_pipeline(const struct yetty_yrender_gpu_resource_binder *self)
{
    const struct yetty_yrender_gpu_resource_binder_impl *impl =
        (const struct yetty_yrender_gpu_resource_binder_impl *)self;
    if (impl->external_pipeline) {
        return yetty_yrender_pipeline_get_pipeline(impl->external_pipeline);
    }
    return impl->pipeline;
}

static WGPUBuffer binder_get_quad_vertex_buffer(
    const struct yetty_yrender_gpu_resource_binder *self)
{
    const struct yetty_yrender_gpu_resource_binder_impl *impl =
        (const struct yetty_yrender_gpu_resource_binder_impl *)self;
    if (impl->external_pipeline) {
        return yetty_yrender_pipeline_get_quad_vertex_buffer(impl->external_pipeline);
    }
    return impl->quad_vertex_buffer;
}

static struct yetty_ycore_void_result binder_write_buffer_chunk(
    struct yetty_yrender_gpu_resource_binder *self, size_t buffer_index, size_t byte_offset,
    const void *data, size_t size)
{
    if (!self) {
        return YETTY_ERR(yetty_ycore_void, "binder is null");
    }
    if (!data || size == 0) {
        return YETTY_OK_VOID();
    }
    struct yetty_yrender_gpu_resource_binder_impl *impl =
        (struct yetty_yrender_gpu_resource_binder_impl *)self;
    if (!impl->finalized || !impl->storage_buffer) {
        /* The binder has handed its GPU allocations back (off-screen figure).
         * The caller keeps the authoritative bytes in its own CPU record and a
         * later finalize() re-uploads the whole buffer, so a direct GPU chunk
         * write here is simply skipped rather than treated as an error. */
        return YETTY_OK_VOID();
    }
    if (buffer_index >= impl->flat_buffer_count) {
        return YETTY_ERR(yetty_ycore_void, "write_buffer_chunk: buffer_index out of range");
    }
    struct flat_buffer *fb = &impl->flat_buffers[buffer_index];
    /* Bounds-check against the rounded GPU slot capacity (fb->cap), not
     * fb->src->size — the slot is what's actually allocated; the logical
     * .size is the high-water mark of CPU-side data that's been declared. */
    if (byte_offset + size > fb->cap) {
        return YETTY_ERR(yetty_ycore_void, "write_buffer_chunk: chunk exceeds buffer capacity");
    }
    /* wgpuQueueWriteBuffer requires the size to be a multiple of 4 (COPY_SIZE
     * alignment) and the offset to be a multiple of 4 (COPY_BUFFER_ALIGNMENT). */
    if ((byte_offset & 3u) != 0u || (size & 3u) != 0u) {
        return YETTY_ERR(yetty_ycore_void,
                         "write_buffer_chunk: offset and size must be 4-byte aligned");
    }
    wgpuQueueWriteBuffer(impl->queue, impl->storage_buffer, fb->mega_offset + byte_offset, data,
                         size);
    return YETTY_OK_VOID();
}

struct yetty_yrender_gpu_resource_binder_result yetty_yrender_gpu_resource_binder_create(
    WGPUDevice device, WGPUQueue queue, WGPUTextureFormat surface_format,
    struct yetty_ydraw_gpu_allocator *allocator)
{
    if (!device) {
        return YETTY_ERR(yetty_yrender_gpu_resource_binder, "device is null");
    }
    if (!queue) {
        return YETTY_ERR(yetty_yrender_gpu_resource_binder, "queue is null");
    }
    if (!allocator) {
        return YETTY_ERR(yetty_yrender_gpu_resource_binder, "allocator is null");
    }

    struct yetty_yrender_gpu_resource_binder_impl *impl =
        calloc(1, sizeof(struct yetty_yrender_gpu_resource_binder_impl));
    if (!impl) {
        return YETTY_ERR(yetty_yrender_gpu_resource_binder, "failed to allocate");
    }

    impl->base.ops = &binder_ops;
    impl->device = device;
    impl->queue = queue;
    impl->surface_format = surface_format;
    impl->allocator = allocator;

    return YETTY_OK(yetty_yrender_gpu_resource_binder, &impl->base);
}

struct yetty_yrender_gpu_resource_binder_result
yetty_yrender_gpu_resource_binder_create_with_pipeline(
    WGPUDevice device, WGPUQueue queue, struct yetty_ydraw_gpu_allocator *allocator,
    const struct yetty_yrender_pipeline *pipeline)
{
    if (!device) {
        return YETTY_ERR(yetty_yrender_gpu_resource_binder, "device is null");
    }
    if (!queue) {
        return YETTY_ERR(yetty_yrender_gpu_resource_binder, "queue is null");
    }
    if (!allocator) {
        return YETTY_ERR(yetty_yrender_gpu_resource_binder, "allocator is null");
    }
    if (!pipeline) {
        return YETTY_ERR(yetty_yrender_gpu_resource_binder, "pipeline is null");
    }

    struct yetty_yrender_gpu_resource_binder_impl *impl =
        calloc(1, sizeof(struct yetty_yrender_gpu_resource_binder_impl));
    if (!impl) {
        return YETTY_ERR(yetty_yrender_gpu_resource_binder, "failed to allocate");
    }

    impl->base.ops = &binder_ops;
    impl->device = device;
    impl->queue = queue;
    /* surface_format unused in two-tier mode (pipeline encodes it) */
    impl->allocator = allocator;
    impl->external_pipeline = pipeline;

    return YETTY_OK(yetty_yrender_gpu_resource_binder, &impl->base);
}
